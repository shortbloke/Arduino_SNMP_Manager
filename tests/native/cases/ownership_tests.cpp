#include "fixtures.h"
#include "registry.h"

void registerOwnershipTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Ownership", name, run}); };
    add("manager and requests share registration lifetime",
        []
        {
            struct Tracked : IntegerCallback
            {
                int &destroyed;
                Tracked(int &n) : destroyed(n) {}
                ~Tracked() override
                {
                    ++destroyed;
                }
            };
            int destroyed = 0;
            Request request;
            {
                Manager manager;
                auto *callback = new Tracked(destroyed);
                callback->OID = strdup(oid);
                manager.addHandler(callback);
                request.addOIDPointer(callback);
                CHECK(request.build());
                CHECK(request.build());
            }
            CHECK(destroyed == 0);
            CHECK(std::string(request.callbacks->value->OID) == oid);
            request.clearOIDList();
            CHECK(destroyed == 1);
            Manager moved = Manager("private");
            Manager destination(std::move(moved));
            CHECK(std::string(destination._community) == "private");
        });
    add("owning packet objects cannot be shallow-copied",
        []
        {
            CHECK(!std::is_copy_constructible<SNMPManager>::value);
            CHECK(!std::is_copy_constructible<SNMPGet>::value);
            CHECK(!std::is_copy_constructible<SNMPGetResponse>::value);
            CHECK(!std::is_copy_constructible<ComplexType>::value);
            struct Tracked : ComplexType
            {
                int &destroyed;
                Tracked(int &n) : ComplexType(STRUCTURE), destroyed(n) {}
                ~Tracked() override
                {
                    ++destroyed;
                }
            };
            int destroyed = 0;
            {
                Request request;
                request.packet = new Tracked(destroyed);
                CHECK(request.build());
                CHECK(destroyed == 1);
                Request moved(std::move(request));
                CHECK(moved.build());
            }
            CHECK(destroyed == 1);
        });
    add("allocation failures leave requests and parsers reusable",
        []
        {
            Manager manager;
            UDP udp;
            int32_t value = 0;
            auto *callback = manager.addIntegerHandler(udp.peer, oid, &value);
            Request request;
            request.setUDP(&udp);
            CHECK(request.addOIDPointer(callback));
            auto wire = message(binding({2, 1, 42}));
            for (int allowance = 0; allowance < 40; ++allowance)
            {
                {
                    FailAllocations fail(allowance);
                    bool built = request.build();
                    CHECK(built || request.packet == nullptr);
                    SNMPGetResponse response;
                    bool parsed = response.parseFrom(wire.data(), wire.size());
                    CHECK(parsed || response.isCorrupt);
                }
                CHECK(request.build());
            }
            {
                FailAllocations fail(0);
                Manager empty;
                CHECK(!empty.addIntegerHandler(udp.peer, oid, &value));
                CHECK(!empty.begin());
            }
            Manager empty;
            {
                FailAllocations fail(0);
                CHECK(!empty.addIntegerHandler(udp.peer, oid, &value));
            }
            CHECK(empty.addIntegerHandler(udp.peer, oid, &value));
        });
    add("callback base supports safe polymorphic destruction",
        []
        {
            CHECK(std::has_virtual_destructor<ValueCallback>::value);
            struct TrackedCallback : IntegerCallback
            {
                int &destroyed;
                explicit TrackedCallback(int &count) : destroyed(count) {}
                ~TrackedCallback() override
                {
                    ++destroyed;
                }
            };
            int destroyed = 0;
            ValueCallback *callback = new TrackedCallback(destroyed);
            delete callback;
            CHECK(destroyed == 1);
        });
    add("nested BER ownership destroys each child once",
        []
        {
            struct TrackedInteger : IntegerType
            {
                int &destroyed;
                explicit TrackedInteger(int &count) : IntegerType(42), destroyed(count) {}
                ~TrackedInteger()
                {
                    ++destroyed;
                }
            };
            int destroyed = 0;
            {
                ComplexType root(STRUCTURE);
                auto *nested = new ComplexType(STRUCTURE);
                nested->addValueToList(new TrackedInteger(destroyed));
                root.addValueToList(nested);
                root.addValueToList(new TrackedInteger(destroyed));
            }
            CHECK(destroyed == 2);
        });
}
