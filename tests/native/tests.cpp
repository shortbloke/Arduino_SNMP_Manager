#include <Arduino_SNMP_Manager.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <limits>
#include <type_traits>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
using Bytes = std::vector<unsigned char>;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(0)
Bytes encode(BER_CONTAINER& value) { unsigned char b[8192]{}; int n=value.serialise(b,sizeof(b)); CHECK(n>=0 && n<=8192); return Bytes(b,b+n); }
// Independent fixture builder: never uses the library's serializer.
Bytes tlv(unsigned char tag, Bytes value) {
    Bytes out{tag};
    if(value.size()<128) out.push_back(value.size());
    else if(value.size()<256) { out.push_back(0x81); out.push_back(value.size()); }
    else { out.push_back(0x82); out.push_back(value.size()>>8); out.push_back(value.size()); }
    out.insert(out.end(),value.begin(),value.end()); return out;
}
Bytes join(std::initializer_list<Bytes> items) { Bytes b; for(auto& i:items) b.insert(b.end(),i.begin(),i.end()); return b; }
const char* oid=".1.3.6.1.2.1.1.1.0";
Bytes oidWire{6,8,43,6,1,2,1,1,1,0};
Bytes binding(Bytes value) { return tlv(0x30,join({oidWire,value})); }
Bytes message(Bytes bindings, int version=1, const char* community="public", int pdu=0xa2, int requestId=7, int errorStatus=0, int errorIndex=0) {
    Bytes c(community,community+strlen(community));
    return tlv(0x30,join({tlv(2,{static_cast<unsigned char>(version)}),tlv(4,c),tlv(pdu,join({tlv(2,{static_cast<unsigned char>(requestId)}),tlv(2,{static_cast<unsigned char>(errorStatus)}),tlv(2,{static_cast<unsigned char>(errorIndex)}),tlv(0x30,bindings)}))}));
}
using Manager = SNMPManager;
struct Request : SNMPGet { Request(int v=1): SNMPGet("public",v) {setRequestID(7);} };
struct Test { const char* name; bool regression; std::function<void()> run; };
#ifdef SNMP_GOOGLETEST
#include <gtest/gtest.h>
#include <cctype>

// GoogleTest owns reporting in the parent. Each case still executes in a child
// so memory errors/timeouts become failures without ending the entire suite.
class IsolatedCase : public testing::Test {
    std::function<void()> run;
public:
    explicit IsolatedCase(std::function<void()> body) : run(body) {}
    void TestBody() override {
        FILE* diagnostics=tmpfile();
        ASSERT_NE(diagnostics, nullptr);
        std::cout.flush(); std::cerr.flush();
        pid_t child=fork();
        if (child==0) {
            if (dup2(fileno(diagnostics), STDERR_FILENO)<0) _exit(2);
            alarm(5);
            try { run(); _exit(0); }
            catch (const std::exception& error) {
                std::cerr << error.what() << std::endl;
                _exit(1);
            }
        }
        int status=0;
        bool reaped=child>0 && waitpid(child, &status, 0)==child;
        rewind(diagnostics);
        std::string output;
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), diagnostics)) output+=buffer;
        fclose(diagnostics);
        ASSERT_TRUE(reaped) << "Could not execute isolated test";
        ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status)==0)
            << "Isolated case failed (wait status " << status << ")\n" << output;
    }
};
#endif

int main(int argc,char** argv) {
    bool regressions=argc>1 && std::string(argv[1])=="--regressions";
    std::vector<Test> tests;
    auto add=[&](const char* name,std::function<void()> f,bool regression=false){tests.push_back({name,regression,f});};
    add("manager and requests share registration lifetime", [] {
        struct Tracked : IntegerCallback {
            int& destroyed;
            Tracked(int& n):destroyed(n) {}
            ~Tracked() override { ++destroyed; }
        };
        int destroyed=0;
        Request request;
        {
            Manager manager;
            auto* callback=new Tracked(destroyed);
            callback->OID=strdup(oid);
            manager.addHandler(callback);
            request.addOIDPointer(callback);
            CHECK(request.build());
            CHECK(request.build());
        }
        CHECK(destroyed==0);
        CHECK(std::string(request.callbacks->value->OID)==oid);
        request.clearOIDList();
        CHECK(destroyed==1);
        Manager moved=Manager("private");
        Manager destination(std::move(moved));
        CHECK(std::string(destination._community)=="private");
    });
    add("bounded primitive decoding and serialization reject short buffers", [] {
        IntegerType integer(128);
        Counter64 counter(UINT64_MAX);
        NetworkAddress address(IPAddress(1,2,3,4));
        NullType null;
        char text[]="hello", name[]=".1.3.6";
        OctetType string(text);
        OIDType oidValue(name);
        ComplexType sequence(STRUCTURE);
        sequence.addValueToList(new IntegerType(42));
        for (BER_CONTAINER* value : std::vector<BER_CONTAINER*>{&integer,&counter,&address,&null,&string,&oidValue,&sequence}) {
            auto wire=encode(*value);
            for (size_t size=0;size<wire.size();++size) {
                Bytes output(wire.size(),0xaa);
                CHECK(value->serialise(output.data(),size)<0);
                CHECK(output==Bytes(wire.size(),0xaa));
            }
            for (size_t size=0;size<wire.size();++size)
                CHECK(!value->fromBuffer(wire.data(),size));
            CHECK(value->fromBuffer(wire.data(),wire.size()));
        }
        Manager manager;
        int destination=0;
        UDP udp;
        Request request;
        request.setUDP(&udp);
        auto* callback=manager.addIntegerHandler(udp.peer,oid,&destination);
        for (int i=0;i<200;++i) request.addOIDPointer(callback);
        CHECK(!request.sendTo(udp.peer));
        CHECK(udp.packets==0);
    });
    add("Opaque payload is preserved without nested decoding", [] {
        auto bytes=message(binding(tlv(0x44,{0xff,0,0x30,0x80,42})));
        SNMPGetResponse response;
        CHECK(response.parseFrom(bytes.data(),bytes.size()));
        CHECK(response.varBinds->value->type==OPAQUE);
        CHECK(response.varBinds->value->value->_isPrimitive);
        CHECK(encode(*response.varBinds->value->value)==Bytes({0x44,5,0xff,0,0x30,0x80,42}));
        auto unknown=message(binding({0x47,0}));
        CHECK(!response.parseFrom(unknown.data(),unknown.size()));
    });
    add("default manager starts without transport", [] {
        SNMPManager manager;
        CHECK(manager._udp==nullptr);
        CHECK(std::string(manager._community)=="public");
        CHECK(!manager.begin() && !manager.loop());
        UDP udp;
        manager.setUDP(&udp);
        CHECK(udp.stops==0 && manager.begin());
    });
    add("integer small wire values",[]{for(unsigned long n: {0UL,1UL,127UL}){IntegerType v(n); CHECK(encode(v)==Bytes({2,1,static_cast<unsigned char>(n)}));}});
    add("integer big endian decode",[]{Bytes b{2,4,0x12,0x34,0x56,0x78}; IntegerType v; CHECK(v.fromBuffer(b.data())); CHECK(v._value==0x12345678UL);});
    add("unsigned application type decoding",[]{Bytes b{0x41,5,0,255,255,255,255}; Counter32 c; Gauge g; TimestampType t; c.fromBuffer(b.data()); b[0]=0x42; g.fromBuffer(b.data()); b[0]=0x43; t.fromBuffer(b.data()); CHECK(c._value==UINT32_MAX); CHECK(g._value==UINT32_MAX); CHECK(t._value==UINT32_MAX);});
    add("counter64 full range decode",[]{Bytes b{0x46,9,0,255,255,255,255,255,255,255,255}; Counter64 c; c.fromBuffer(b.data()); CHECK(c._value==UINT64_MAX); Counter64 zero(0); CHECK(encode(zero)==Bytes({0x46,1,0}));});
    add("null and network address",[]{NullType n; CHECK(encode(n)==Bytes({5,0})); NetworkAddress ip(IPAddress(192,0,2,1)); auto b=encode(ip); CHECK(b==Bytes({0x40,4,192,0,2,1})); NetworkAddress decoded; decoded.fromBuffer(b.data()); CHECK(decoded._value==IPAddress(192,0,2,1));});
    add("OID wire bytes and enterprise arc",[]{char s[]=".1.3.6.1.4.1.12345.0"; OIDType o(s); auto b=encode(o); CHECK(b==Bytes({6,8,43,6,1,4,1,0xe0,0x39,0})); OIDType d; d.fromBuffer(b.data()); CHECK(std::string(d._value)==s);});
    add("octet decode length boundaries",[]{for(size_t n: {0,1,127,128,255,256,257,1023}){auto b=tlv(4,Bytes(n,'x')); OctetType o; CHECK(o.fromBuffer(b.data())); CHECK(o.getLength()==n); CHECK(std::string(o._value)==std::string(n,'x'));}});
    add("octet encode length boundaries",[]{for(size_t n: {0,1,127,128,255,257}){OctetType o; memset(o._value,0,sizeof(o._value)); memset(o._value,'x',n); CHECK(encode(o)==tlv(4,Bytes(n,'x')));}});
    add("nested BER structure",[]{auto b=tlv(0x30,join({{2,1,42},tlv(0x30,{5,0})})); ComplexType c(STRUCTURE); CHECK(c.fromBuffer(b.data())); CHECK(c._values->value->_type==INTEGER); CHECK(c._values->next->value->_type==STRUCTURE); CHECK(encode(c)==b);});
    add("response metadata and multiple bindings",[]{for(int version: {0,1}){auto b=message(join({binding({2,1,42}),binding({0x43,2,1,0})}),version); SNMPGetResponse r; CHECK(r.parseFrom(b.data())); CHECK(r.version==version+1); CHECK(r.requestID==7); CHECK(r.errorStatus==0 && r.errorIndex==0); CHECK(std::string(r.communityString)=="public"); CHECK(r.requestType==GetResponsePDU); CHECK(r.varBinds->value->type==INTEGER); CHECK(r.varBinds->next->value->type==TIMESTAMP); CHECK(r.varBinds->next->next->value==nullptr);}});
    add("response rejects wrong top-level tag",[]{Bytes b{4,0}; SNMPGetResponse r; CHECK(!r.parseFrom(b.data())); CHECK(r.isCorrupt);});
    add("response rejects wrong version field type",[]{auto b=message(binding({2,1,42})); b[2]=4; SNMPGetResponse r; CHECK(!r.parseFrom(b.data())); CHECK(r.isCorrupt);});
    add("request missing transport",[]{Request r; CHECK(!r.sendTo(IPAddress()));});
    add("request golden wire and ports",[]{for(int version:{0,1}){Manager m; int value=0; UDP udp; Request r(version); r.setUDP(&udp); r.addOIDPointer(m.addIntegerHandler(udp.peer,oid,&value)); CHECK(r.sendTo(udp.peer)); CHECK(udp.outgoing==message(binding({5,0}),version,"public",0xa0)); CHECK(udp.destination==udp.peer); CHECK(udp.destinationPort==161); r.setPort(1161); udp.endResult=0; CHECK(!r.sendTo(udp.peer)); CHECK(udp.destinationPort==1161);}});
    add("request list ordering and clearing",[]{Manager m; int v=0; auto* a=m.addIntegerHandler(IPAddress(),oid,&v); auto* b=m.addIntegerHandler(IPAddress(),".1.3.6.1.2.1.1.3.0",&v); Request r; r.addOIDPointer(a); r.addOIDPointer(b); CHECK(r.callbacks->value==a && r.callbacks->next->value==b); r.clearOIDList(); CHECK(r.callbacks->value==nullptr); CHECK(std::string(a->OID)==oid); r.addOIDPointer(b); CHECK(r.callbacks->value==b);});
    add("callback identity requires IP and OID",[]{Manager m; int v=0; IPAddress ip(1,2,3,4); CHECK(!m.findCallback(ip,oid)); auto* a=m.addIntegerHandler(ip,oid,&v); CHECK(m.findCallback(ip,oid)==a); CHECK(!m.findCallback(IPAddress(),oid)); CHECK(!m.findCallback(ip,".1.3.6.1.2.1.1.2.0"));});
    add("library convention: UDP lifecycle and integer dispatch",[]{Manager m; UDP a,b; int v=0; CHECK(!m.begin()); CHECK(!m.loop()); m.setUDP(&a); CHECK(a.listenPort==162); m.setUDP(&b); CHECK(a.stops==1); m.addIntegerHandler(b.peer,oid,&v); CHECK(m.loop()); CHECK(b.reads==0); b.incoming=message(binding({2,1,42})); CHECK(m.loop()); CHECK(v==42); CHECK(b.reads==1 && b.flushes==1);});
    add("manager unsigned typed dispatch",[]{for(int tag:{0x41,0x42,0x43,0x46}){Manager m; UDP u; m.setUDP(&u); uint32_t n=0; uint64_t big=0; if(tag==0x41)m.addCounter32Handler(u.peer,oid,&n); if(tag==0x42)m.addGaugeHandler(u.peer,oid,&n); if(tag==0x43)m.addTimestampHandler(u.peer,oid,&n); if(tag==0x46)m.addCounter64Handler(u.peer,oid,&big); u.incoming=message(binding(tlv(tag,{0,255,255,255,255}))); m.loop(); CHECK(tag==0x46 ? big==UINT32_MAX : n==UINT32_MAX);}});
    add("community, peer and callback type rejection",[]{for(int scenario=0;scenario<3;++scenario){Manager m; UDP u; m.setUDP(&u); int value=99; m.addIntegerHandler(u.peer,oid,&value); Bytes val=scenario==2 ? Bytes{4,1,'x'} : Bytes{2,1,42}; u.incoming=message(binding(val),1,scenario==0?"private":"public"); if(scenario==1)u.peer=IPAddress(); m.loop(); CHECK(value==99);}});
    // RFC 3416 section 4.2.1: exceptions are values, not malformed packets.
    add("v2c Get exceptions parse as individual bindings",[]{for(int tag:{0x80,0x81}){auto b=message(binding(tlv(tag,{}))); SNMPGetResponse r; CHECK(r.parseFrom(b.data())); CHECK(r.errorStatus==0); CHECK(r.varBinds->value->type==tag);}});
    add("traversal endOfMibView parses as an exception",[]{auto b=message(binding({0x82,0})); SNMPGetResponse r; CHECK(r.parseFrom(b.data())); CHECK(r.varBinds->value->type==ENDOFMIBVIEW);});
    add("Integer32 serialization byte and sign boundaries", [] {
        const std::vector<std::pair<int32_t,Bytes>> fixtures{
            {127,{127}}, {128,{0,128}}, {255,{0,255}}, {256,{1,0}},
            {32767,{0x7f,255}}, {32768,{0,0x80,0}},
            {-1,{255}}, {-128,{128}}, {-129,{255,127}},
            {-32768,{128,0}}, {-32769,{255,127,255}},
            {INT32_MAX,{127,255,255,255}}, {INT32_MIN,{128,0,0,0}}
        };
        for (const auto& fixture : fixtures) {
            IntegerType value(static_cast<unsigned long>(fixture.first));
            const auto original=value._value;
            CHECK(encode(value)==tlv(2,fixture.second));
            CHECK(value._value==original);
            CHECK(encode(value)==tlv(2,fixture.second));
        }
        Counter32 counter(UINT32_MAX);
        Gauge gauge(UINT32_MAX);
        TimestampType ticks(UINT32_MAX);
        CHECK(encode(counter)==Bytes({0x41,5,0,255,255,255,255}));
        CHECK(encode(gauge)==Bytes({0x42,5,0,255,255,255,255}));
        CHECK(encode(ticks)==Bytes({0x43,5,0,255,255,255,255}));
    });
    add("integer 128 minimal signed BER encoding",[]{IntegerType v(128); CHECK(encode(v)==Bytes({2,2,0,128}));});
    add("integer serialization preserves value",[]{IntegerType v(256); encode(v); CHECK(v._value==256);});
    add("octet 256 length encoding",[]{OctetType v; memset(v._value,0,sizeof(v._value)); memset(v._value,'x',256); CHECK(encode(v)==tlv(4,Bytes(256,'x')));});
    add("OID base128 boundary 16384",[]{char s[]=".1.3.16384"; OIDType v(s); CHECK(encode(v)==Bytes({6,4,43,0x81,0x80,0}));});
    add("manager rejects unsupported version", [] {
        for (int version : {2, 3, 127}) {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value=99;
            manager.addIntegerHandler(udp.peer,oid,&value);
            udp.incoming=message(binding({2,1,42}),version);
            manager.loop();
            CHECK(value==99);
            // Rejection must leave the manager able to process v1 and v2c.
            for (int supported : {0, 1}) {
                value=99;
                udp.incoming=message(binding({2,1,42}),supported);
                manager.loop();
                CHECK(value==42);
            }
        }
    });
    add("library convention: float callback preserves fractional tenths", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        float value=99;
        manager.addFloatHandler(udp.peer,oid,&value);
        for (unsigned char raw : {0, 1, 10, 123}) {
            udp.incoming=message(binding({2,1,raw}));
            manager.loop();
            CHECK(std::abs(value-static_cast<float>(raw)/10.0f)<0.001f);
        }
        udp.incoming=message(binding({2,1,0x85})); // -123 integer tenths
        manager.loop();
        CHECK(std::abs(value+12.3f)<0.001f);
    });
    add("library convention: shorter string response terminates old value", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        char storage[32]="previous value";
        char* value=storage;
        manager.addStringHandler(udp.peer,oid,&value);
        for (const char* text : {"new", "", "longer again", "x"}) {
            udp.incoming=message(binding(tlv(4,Bytes(text,text+strlen(text)))));
            manager.loop();
            CHECK(std::string(value)==text);
        }
    });
    // RFC 3417 section 8 permits nonminimal definite-length fields.
    add("octet nonminimal definite length accepted",[]{Bytes b{4,0x82,0,3,'a','b','c'}; OctetType v; CHECK(v.fromBuffer(b.data())); CHECK(v.getLength()==3); CHECK(std::string(v._value)=="abc");});
    add("response nonminimal outer length accepted",[]{auto b=message(binding({2,1,42})); b.insert(b.begin()+1,{0x82,0}); SNMPGetResponse r; CHECK(r.parseFrom(b.data())); CHECK(r.requestID==7); CHECK(r.varBinds->value->type==INTEGER);});
    add("v1 noSuchName metadata retains one-based error index",[]{auto b=message(binding({5,0}),0,"public",0xa2,7,2,1); SNMPGetResponse r; CHECK(r.parseFrom(b.data())); CHECK(r.version==1); CHECK(r.errorStatus==2 && r.errorIndex==1);});
    add("successful Get preserves OID order on wire",[]{Manager m; UDP u; int n=0; Request r; r.setUDP(&u); r.addOIDPointer(m.addIntegerHandler(u.peer,oid,&n)); r.addOIDPointer(m.addIntegerHandler(u.peer,".1.3.6.1.2.1.1.3.0",&n)); Bytes second=oidWire; second[8]=3; CHECK(r.sendTo(u.peer)); CHECK(u.outgoing==message(join({binding({5,0}),tlv(0x30,join({second,{5,0}}))}),1,"public",0xa0));});
    add("manager receives 484-byte response",[]{Manager m; UDP u; m.setUDP(&u); char storage[512]{}; char* ptr=storage; m.addStringHandler(u.peer,oid,&ptr); u.incoming=message(binding(tlv(4,Bytes(434,'x')))); CHECK(u.incoming.size()==484); m.loop(); CHECK(std::string(ptr)==std::string(434,'x'));});
    // X.690 section 8.3: signed values require sign extension on decode.
    add("Integer32 decoding boundaries and definite lengths", [] {
        for (Bytes bytes : {Bytes{2,4,0x80,0,0,0}, Bytes{2,0x81,4,0x80,0,0,0},
                            Bytes{2,0x82,0,4,0x80,0,0,0}}) {
            IntegerType value;
            CHECK(value.fromBuffer(bytes.data()));
            CHECK(value._value==static_cast<unsigned long>(INT32_MIN));
            CHECK(value.getLength()==4);
        }
        Bytes positive{2,4,0x7f,255,255,255};
        IntegerType value;
        CHECK(value.fromBuffer(positive.data()));
        CHECK(value._value==INT32_MAX);
        for (Bytes bytes : {Bytes{2,0}, Bytes{2,0x80}, Bytes{2,0xff}, Bytes{2,5},
                            Bytes{2,0x82,1,0}}) {
            IntegerType invalid(42);
            CHECK(!invalid.fromBuffer(bytes.data()));
            CHECK(invalid._value==42);
        }
    });
    add("negative INTEGER sign extension",[]{for(auto b:{Bytes{2,1,0xff},Bytes{2,1,0x80},Bytes{2,2,0xff,0x7f}}){IntegerType v; CHECK(v.fromBuffer(b.data())); long expected=b.size()==4 ? -129 : (b[2]==0xff ? -1 : -128); CHECK(v._value==static_cast<unsigned long>(expected));}});
    add("Integer32 signed boundary encoding",[]{IntegerType v(static_cast<unsigned long>(INT32_MIN)); CHECK(encode(v)==Bytes({2,4,0x80,0,0,0}));});
    add("Counter64 small value uses minimal contents", [] {
        const std::vector<std::pair<uint64_t,Bytes>> fixtures{
            {0,{0}}, {1,{1}}, {127,{127}}, {128,{0,128}},
            {255,{0,255}}, {256,{1,0}},
            {UINT64_C(0x7fffffffffffffff),{0x7f,255,255,255,255,255,255,255}},
            {UINT64_C(0x8000000000000000),{0,0x80,0,0,0,0,0,0,0}}
        };
        for (const auto& fixture : fixtures) {
            Counter64 value(fixture.first);
            CHECK(encode(value)==tlv(0x46,fixture.second));
            CHECK(value._value==fixture.first);
            CHECK(encode(value)==tlv(0x46,fixture.second));
        }
    });
    add("Counter64 maximum has positive sign octet",[]{Counter64 v(UINT64_MAX); CHECK(encode(v)==Bytes({0x46,9,0,255,255,255,255,255,255,255,255}));});
    add("unsigned application encoding preserves positive sign",[]{Counter32 v(UINT32_MAX); CHECK(encode(v)==Bytes({0x41,5,0,255,255,255,255}));});
    add("oversized OCTET STRING is rejected without truncation", [] {
        auto bytes=tlv(4,Bytes(SNMP_OCTETSTRING_MAX_LENGTH,'x'));
        OctetType value;
        CHECK(!value.fromBuffer(bytes.data()));
        char output[2048]{};
        std::string oversized(SNMP_OCTETSTRING_MAX_LENGTH,'x');
        OctetType constructed(&oversized[0]);
        CHECK(constructed.serialise(reinterpret_cast<unsigned char*>(output))<0);
    });
    add("binary OCTET STRING preserves embedded zero",[]{auto b=tlv(4,{'a',0,'b'}); OctetType v; CHECK(v.fromBuffer(b.data())); CHECK(v.getLength()==3); CHECK(memcmp(v._value,"a\0b",3)==0);});
    add("binary OCTET STRING re-encoding preserves length",[]{auto b=tlv(4,{'a',0,'b'}); OctetType v; CHECK(v.fromBuffer(b.data())); CHECK(encode(v)==b);});
    add("OID roots and malformed subidentifiers", [] {
        const std::vector<std::pair<std::string,Bytes>> fixtures{
            {".0.0",{0}}, {".1.39.0",{79,0}}, {".2.0.0",{80,0}},
            {".2.4294967295.0",{0x90,0x80,0x80,0x80,0x4f,0}}
        };
        for (auto fixture : fixtures) {
            OIDType value(&fixture.first[0]);
            const auto wire=tlv(6,fixture.second);
            CHECK(encode(value)==wire);
            auto input=wire;
            OIDType decoded;
            CHECK(decoded.fromBuffer(input.data()));
            CHECK(input==wire);
            CHECK(std::string(decoded._value)==fixture.first);
        }
        for (Bytes bytes : {Bytes{6,0}, Bytes{6,1,0x81}, Bytes{6,2,0x80,0},
                            Bytes{6,6,43,0x90,0x80,0x80,0x80,0}}) {
            OIDType value;
            CHECK(!value.fromBuffer(bytes.data()));
        }
    });
    add("OID first arcs are decoded rather than assumed",[]{Bytes b{6,3,0x88,0x37,3}; OIDType v; CHECK(v.fromBuffer(b.data())); CHECK(std::string(v._value)==".2.999.3");});
    add("OID maximum subidentifier encodes five base128 octets",[]{char oid[]=".1.3.4294967295"; OIDType v(oid); CHECK(encode(v)==Bytes({6,6,43,0x8f,0xff,0xff,0xff,0x7f}));});
    add("bounded response parser rejects every truncated prefix", [] {
        const auto packet=message(binding({2,1,42}));
        for (size_t length=0; length<packet.size(); ++length) {
            Bytes prefix(packet.begin(),packet.begin()+length);
            SNMPGetResponse response;
            CHECK(!response.parseFrom(prefix.data(),prefix.size()));
        }
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int value=99;
        manager.addIntegerHandler(udp.peer,oid,&value);
        udp.incoming=Bytes(SNMP_PACKET_LENGTH*3+1,0);
        manager.loop();
        CHECK(value==99 && udp.reads==0);
    });
    add("indefinite sequence length rejected",[]{Bytes b{0x30,0x80,2,1,42,0,0}; ComplexType v(STRUCTURE); CHECK(!v.fromBuffer(b.data()));});
    // RFC 3416 section 4.2.1 requires an empty list in the alternate tooBig response.
    add("response parser safely handles empty and missing fields on reuse", [] {
        SNMPGetResponse response;
        auto valid=message(binding({2,1,42}));
        CHECK(response.parseFrom(valid.data(),valid.size()));
        auto empty=message({});
        CHECK(response.parseFrom(empty.data(),empty.size()));
        CHECK(!response.varBinds->value);
        for (Bytes malformed : {tlv(0x30,{}), message(tlv(0x30,{})),
                                message(tlv(0x30,oidWire))}) {
            CHECK(!response.parseFrom(malformed.data(),malformed.size()));
            CHECK(response.isCorrupt);
        }
        CHECK(response.parseFrom(valid.data(),valid.size()));
        CHECK(!response.isCorrupt && response.varBinds->value->type==INTEGER);
    });
    add("short tooBig response with empty bindings accepted",[]{auto b=message({},1,"public",0xa2,7,1,0); CHECK(b.size()<30); SNMPGetResponse r; CHECK(r.parseFrom(b.data())); CHECK(r.errorStatus==1 && r.errorIndex==0); CHECK(!r.varBinds || !r.varBinds->value);});
    add("empty bindings accepted with long community",[]{auto b=message({},1,"long-community-name",0xa2,7,1,0); CHECK(b.size()>30); SNMPGetResponse r; CHECK(r.parseFrom(b.data())); CHECK(r.errorStatus==1); CHECK(!r.varBinds || !r.varBinds->value);});
    add("PDU-level errors must not update values", [] {
        for (int version : {0, 1}) {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int first=99,second=99;
            manager.addIntegerHandler(udp.peer,oid,&first);
            manager.addIntegerHandler(udp.peer,".1.3.6.1.2.1.1.3.0",&second);
            Bytes other=oidWire;
            other[8]=3;
            auto bindings=join({binding({2,1,42}),tlv(0x30,join({other,{2,1,7}}))});
            for (int error : {1, 2, 3, 4, 5}) {
                udp.incoming=message(bindings,version,"public",0xa2,7,error,error==1 ? 0 : 2);
                manager.loop();
                CHECK(first==99 && second==99);
            }
            udp.incoming=message(bindings,version);
            manager.loop();
            CHECK(first==42 && second==7);
        }
    });
    add("exception binding does not discard following success",[]{for(int tag:{0x80,0x81,0x82}){Manager m; UDP u; m.setUDP(&u); int missing=99,success=0; m.addIntegerHandler(u.peer,oid,&missing); m.addIntegerHandler(u.peer,".1.3.6.1.2.1.1.3.0",&success); Bytes second=oidWire; second[8]=3; u.incoming=message(join({binding(tlv(tag,{})),tlv(0x30,join({second,{2,1,42}}))})); m.loop(); CHECK(missing==99); CHECK(success==42);}});
    add("response with matching outstanding request ID updates value",[]{Manager m; UDP u; m.setUDP(&u); int n=99; Request r; r.setUDP(&u); r.addOIDPointer(m.addIntegerHandler(u.peer,oid,&n)); CHECK(r.sendTo(u.peer)); u.incoming=message(binding({2,1,42}),1,"public",0xa2,7); m.loop(); CHECK(n==42);});
    add("request tracking is independent per callback and requires a complete send", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int first=99,second=99;
        IPAddress otherPeer(192,0,2,2);
        auto* a=manager.addIntegerHandler(udp.peer,oid,&first);
        auto* b=manager.addIntegerHandler(otherPeer,oid,&second);
        Request one,two;
        one.setUDP(&udp); two.setUDP(&udp);
        one.addOIDPointer(a); two.addOIDPointer(b);
        udp.beginPacketResult=0;
        CHECK(!one.sendTo(udp.peer) && !a->requestTracked);
        udp.beginPacketResult=1; udp.writeLimit=1;
        CHECK(!one.sendTo(udp.peer) && !a->requestTracked);
        udp.writeLimit=static_cast<size_t>(-1);
        CHECK(one.sendTo(udp.peer));
        two.setRequestID(8);
        CHECK(two.sendTo(otherPeer));
        udp.incoming=message(binding({2,1,42}));
        manager.loop();
        CHECK(first==42 && second==99 && b->requestPending);
        udp.peer=otherPeer;
        udp.incoming=message(binding({2,1,7}),1,"public",0xa2,8);
        manager.loop();
        CHECK(first==42 && second==7 && !b->requestPending);
    });
    add("request tracking handles superseded failed and duplicate replies", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int value=99;
        auto* callback=manager.addIntegerHandler(udp.peer,oid,&value);
        Request request;
        request.setUDP(&udp);
        request.addOIDPointer(callback);
        CHECK(request.sendTo(udp.peer)); // 7
        request.setRequestID(8);
        CHECK(request.sendTo(udp.peer));
        request.setRequestID(9);
        udp.endResult=0;
        CHECK(!request.sendTo(udp.peer));
        auto reply=[&](int id, int contents) {
            udp.incoming=message(binding({2,1,static_cast<unsigned char>(contents)}),1,"public",0xa2,id);
            manager.loop();
        };
        reply(7,1); CHECK(value==99);
        reply(9,2); CHECK(value==99);
        reply(8,42); CHECK(value==42);
        reply(8,3); CHECK(value==42);
        udp.endResult=1;
        request.setRequestID(10);
        CHECK(request.sendTo(udp.peer));
        udp.incoming=message({},1,"public",0xa2,10,1,0);
        manager.loop();
        CHECK(!callback->requestPending);
        reply(10,4); CHECK(value==42);
    });
    add("response must match outstanding request ID",[]{Manager m; UDP u; m.setUDP(&u); int n=99; Request r; r.setUDP(&u); r.addOIDPointer(m.addIntegerHandler(u.peer,oid,&n)); CHECK(r.sendTo(u.peer)); u.incoming=message(binding({2,1,42}),1,"public",0xa2,8); m.loop(); CHECK(n==99);});
    add("sequence content exactly 256 has a two-octet length", [] {
        ComplexType sequence(STRUCTURE);
        Bytes content;
        for (int i=0; i<128; ++i) {
            sequence.addValueToList(new NullType());
            content.insert(content.end(), {5,0});
        }
        CHECK(content.size()==256);
        CHECK(encode(sequence)==tlv(0x30,content));
    });

    add("sequence lengths either side of 256", [] {
        for (size_t contentLength : {127,128,255,257}) {
            const size_t payloadLength=contentLength-(contentLength<130 ? 2 : 3);
            auto* value=new OctetType();
            memset(value->_value,0,sizeof(value->_value));
            memset(value->_value,'x',payloadLength);
            ComplexType sequence(STRUCTURE);
            sequence.addValueToList(value);
            auto content=tlv(4,Bytes(payloadLength,'x'));
            CHECK(content.size()==contentLength);
            CHECK(encode(sequence)==tlv(0x30,content));
        }
    });

    // Verify consumed-byte accounting keeps siblings aligned.
    add("long-form nested child leaves sibling aligned", [] {
        for (size_t size : {128,256,300}) {
            auto bytes=tlv(0x30,join({tlv(0x30,tlv(4,Bytes(size,'x'))),{2,1,42}}));
            ComplexType root(STRUCTURE);
            CHECK(root.fromBuffer(bytes.data()));
            CHECK(root._values && root._values->next);
            CHECK(root._values->next->value->_type==INTEGER);
            CHECK(static_cast<IntegerType*>(root._values->next->value)->_value==42);
            CHECK(root._values->next->next==nullptr);
            auto* child=static_cast<ComplexType*>(root._values->value);
            CHECK(child->_values->value->_type==STRING);
            CHECK(static_cast<OctetType*>(child->_values->value)->getLength()==size);
        }
    });

    add("Counter64 handles a long-form length header", [] {
        for (Bytes bytes : {Bytes{0x46,0x81,1,42}, Bytes{0x46,0x82,0,1,42},
                            Bytes{0x46,0x83,0,0,1,42}}) {
            Counter64 value;
            CHECK(value.fromBuffer(bytes.data()));
            CHECK(value._value==42);
            CHECK(value.getLength()==1);
        }
        Bytes maximum{0x46,0x81,9,0,255,255,255,255,255,255,255,255};
        Counter64 value;
        CHECK(value.fromBuffer(maximum.data()));
        CHECK(value._value==UINT64_MAX && value.getLength()==9);
    });
    add("Counter64 rejects invalid lengths and out-of-range contents", [] {
        for (Bytes bytes : {Bytes{0x46,0}, Bytes{0x46,0x80}, Bytes{0x46,0xff},
                            Bytes{0x46,10}, Bytes{0x46,0x82,1,0},
                            Bytes{0x46,1,0xff}, Bytes{0x46,9,1,0,0,0,0,0,0,0,0}}) {
            Counter64 value(42);
            CHECK(!value.fromBuffer(bytes.data()));
            CHECK(value._value==42);
        }
    });

    add("child length cannot exceed enclosing sequence", [] {
        // Backing bytes exist, but the parent declares only three content bytes.
        Bytes bytes{0x30,3,4,5,'a','b','c','d','e'};
        ComplexType root(STRUCTURE);
        CHECK(!root.fromBuffer(bytes.data()));
    });

    add("dangling child tag must not be silently skipped", [] {
        // The final INTEGER tag is inside the parent; its length is missing.
        Bytes bytes{0x30,3,5,0,2,0};
        ComplexType root(STRUCTURE);
        CHECK(!root.fromBuffer(bytes.data()));
    });

    add("three-byte negative INTEGER reaches signed callback", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int value=99;
        manager.addIntegerHandler(udp.peer,oid,&value);
        udp.incoming=message(binding({2,3,0xff,0xff,0x7f}));
        manager.loop();
        CHECK(value==-129);
    });

    add("UDP bind failure is reported", [] {
        Manager manager;
        UDP udp;
        udp.beginResult=0;
        manager.setUDP(&udp);
        CHECK(!manager.begin());
        udp.beginResult=1;
        CHECK(manager.begin());
        CHECK(udp.listenPort==162);
    });

    add("embedded NUL community cannot match public prefix", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int value=99;
        manager.addIntegerHandler(udp.peer,oid,&value);
        auto valid=message(binding({2,1,42}));
        // Replace the independently generated community field, then rewrap.
        Bytes body(valid.begin()+2,valid.end());
        body.erase(body.begin()+3,body.begin()+11);
        auto community=tlv(4,{'p','u','b','l','i','c',0,'x'});
        body.insert(body.begin()+3,community.begin(),community.end());
        udp.incoming=tlv(0x30,body);
        manager.loop();
        CHECK(value==99);
    });

    add("over-cap community must not match truncated prefix", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int value=99;
        manager.addIntegerHandler(udp.peer,oid,&value);
        std::string configured(253,'a');
        manager._community=configured.c_str();
        std::string incoming(SNMP_OCTETSTRING_MAX_LENGTH,'a');
        incoming.back()='b';
        auto bytes=message(binding({2,1,42}),1,incoming.c_str());
        CHECK(bytes.size()<SNMP_PACKET_LENGTH*3);
        // Exercise the public parser helper to isolate community handling from
        // the separate 512-byte UDP read cap. Every byte of the TLV is present.
        std::string hex;
        for (auto byte : bytes) {
            char token[4];
            snprintf(token,sizeof(token),"%02x ",byte);
            hex+=token;
        }
        manager.testParsePacket(String(hex.c_str()));
        CHECK(value==99);
    });

    add("incomplete UDP response cannot update a callback", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int value=99;
        manager.addIntegerHandler(udp.peer,oid,&value);
        udp.incoming=message(binding({2,1,42}));
        udp.incoming.pop_back(); // INTEGER content is missing, lengths unchanged.
        manager.loop();
        CHECK(value==99);
    });

    // The manager does not currently delete callbacks; check the public API
    // supports callers deleting them through the base type.
    add("callback base supports safe polymorphic destruction", [] {
        CHECK(std::has_virtual_destructor<ValueCallback>::value);
        struct TrackedCallback : IntegerCallback {
            int& destroyed;
            explicit TrackedCallback(int& count) : destroyed(count) {}
            ~TrackedCallback() override { ++destroyed; }
        };
        int destroyed=0;
        ValueCallback* callback=new TrackedCallback(destroyed);
        delete callback;
        CHECK(destroyed==1);
    });

    add("nested BER ownership destroys each child once", [] {
        struct TrackedInteger : IntegerType {
            int& destroyed;
            explicit TrackedInteger(int& count) : IntegerType(42), destroyed(count) {}
            ~TrackedInteger() { ++destroyed; }
        };
        int destroyed=0;
        {
            ComplexType root(STRUCTURE);
            auto* nested=new ComplexType(STRUCTURE);
            nested->addValueToList(new TrackedInteger(destroyed));
            root.addValueToList(nested);
            root.addValueToList(new TrackedInteger(destroyed));
        }
        CHECK(destroyed==2);
    });
    add("long OID request and response preserve callback identity", [] {
        for (size_t arcs : {24, 60}) {
            std::string name=".1.3";
            Bytes contents{43};
            for (size_t i=0; i<arcs; ++i) { name+=".1"; contents.push_back(1); }
            CHECK(name.size()>50 && name.size()<MAX_OID_LENGTH);
            Manager manager;
            UDP udp;
            int value=99;
            manager.setUDP(&udp);
            auto* callback=manager.addIntegerHandler(udp.peer,name.c_str(),&value);
            CHECK(manager.findCallback(udp.peer,name.c_str())==callback);
            Request request;
            request.setUDP(&udp);
            request.addOIDPointer(callback);
            CHECK(request.sendTo(udp.peer));
            auto wireOID=tlv(6,contents);
            CHECK(udp.outgoing==message(tlv(0x30,join({wireOID,{5,0}})),1,"public",0xa0));
            udp.incoming=message(tlv(0x30,join({wireOID,{2,1,42}})));
            manager.loop();
            CHECK(value==42);
        }
    });
    add("same OID responses update only the matching device", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        IPAddress first(192,0,2,1), second(192,0,2,2);
        int a=99,b=99;
        manager.addIntegerHandler(first,oid,&a);
        manager.addIntegerHandler(second,oid,&b);
        udp.peer=second;
        udp.incoming=message(binding({2,1,42}));
        manager.loop();
        CHECK(a==99 && b==42);
        udp.peer=first;
        udp.incoming=message(binding({2,1,7}));
        manager.loop();
        CHECK(a==7 && b==42);
    });
    add("unregistered OID response leaves callbacks intact", [] {
        Manager manager;
        UDP udp;
        manager.setUDP(&udp);
        int value=99;
        manager.addIntegerHandler(udp.peer,oid,&value);
        Bytes other=oidWire;
        other[8]=2;
        udp.incoming=message(tlv(0x30,join({other,{2,1,42}})));
        manager.loop();
        CHECK(value==99);
        udp.incoming=message(binding({2,1,7}));
        manager.loop();
        CHECK(value==7);
    });
    add("OID four-octet subidentifier encoding", [] {
        char name[]=".1.3.268435455.0";
        OIDType value(name);
        CHECK(encode(value)==Bytes({6,6,43,0xff,0xff,0xff,0x7f,0}));
    });
    add("OID four-octet boundary encoding", [] {
        char name[]=".1.3.2097152.0";
        OIDType value(name);
        CHECK(encode(value)==Bytes({6,6,43,0x81,0x80,0x80,0,0}));
    });
    add("OID ten-digit segment encoding preserves following arc", [] {
        // A nonterminal segment also exercises copying the trailing dot.
        char name[]=".1.3.1000000000.0";
        OIDType value(name);
        CHECK(encode(value)==Bytes({6,7,43,0x83,0xdc,0xeb,0x94,0,0}));
    });
    add("OID unsigned ten-digit segment decoding", [] {
        Bytes bytes{6,7,43,0x8f,0xff,0xff,0xff,0x7f,0};
        OIDType value;
        CHECK(value.fromBuffer(bytes.data()));
        CHECK(std::string(value._value)==".1.3.4294967295.0");
    });

#ifdef SNMP_GOOGLETEST
    testing::InitGoogleTest(&argc, argv);
    for (const auto& test : tests) {
        std::string name=test.name;
        for (char& c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c='_';
        }
        testing::RegisterTest(test.regression ? "Regression" : "Baseline",
            name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
            [test]() -> IsolatedCase* { return new IsolatedCase(test.run); });
    }
    // PlatformIO derives failure status from GoogleTest output. A nonzero
    // executable exit adds a spurious infrastructure error to its case count.
    const int result=RUN_ALL_TESTS();
    (void)result;
    std::cout.flush();
    std::cerr.flush();
    return 0;
#else
    int failed=0, count=0;
    // Isolate every case so malformed inputs cannot stop the rest of the suite.
    // A crash, sanitizer abort, timeout, or assertion always counts as failure.
    for(auto& t:tests) if(t.regression==regressions) {
        ++count;
        std::cout.flush(); std::cerr.flush();
        pid_t child=fork();
        if(child==0) {
            alarm(5);
            try { t.run(); std::cout.flush(); _exit(0); }
            catch(const std::exception& e) { std::cerr<<"  "<<e.what()<<std::endl; _exit(1); }
        }
        int status=0;
        bool ok=child>0 && waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0;
        if(!ok) ++failed;
        std::cout<<(ok ? "PASS " : "FAIL ")<<t.name;
        if(child>0 && WIFSIGNALED(status)) std::cout<<" (signal "<<WTERMSIG(status)<<")";
        std::cout<<std::endl;
    }
    std::cout<<count<<" tests, "<<failed<<" failures"<<std::endl;
    return failed?1:0;
#endif
}
