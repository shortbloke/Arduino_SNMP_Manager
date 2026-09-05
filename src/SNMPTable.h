#ifndef SNMP_TABLE_H
#define SNMP_TABLE_H
#include "SNMPClient.h"
#include <cstring>

struct SNMPCell
{
    SNMPValue value;
    SNMPStatus status = SNMPStatus::Missing;
    bool ok() const
    {
        return status.ok();
    }
};

template <size_t Columns, size_t IndexCapacity = MAX_OID_LENGTH> struct SNMPTableRow
{
    char index[IndexCapacity] = {}; // Entire suffix, including composite indices.
    SNMPCell cells[Columns];
    const SNMPCell &operator[](size_t column) const
    {
        return cells[column];
    }
};

// Walk selected columns one at a time and join them by the complete index suffix.
// The table owns all rows; a single streaming walk supplies bounded working state.
template <size_t Rows, size_t Columns, size_t IndexCapacity = MAX_OID_LENGTH> class SNMPTableRead
{
public:
    explicit SNMPTableRead(SNMPDevice &device) : walk_(device)
    {
        walk_.stream(consume, this);
        walk_.onComplete(advance, this);
    }
    SNMPTableRead(const SNMPTableRead &) = delete;
    SNMPTableRead &operator=(const SNMPTableRead &) = delete;
    SNMPStatus addColumn(const char *oid, ASN_TYPE expected = NULLTYPE,
                         const char *fallback = nullptr, ASN_TYPE fallbackExpected = NULLTYPE)
    {
        if (pending())
            return SNMPStatus::Busy;
        if (columns_ == Columns)
            return SNMPStatus::CapacityExceeded;
        if (!oid || strlen(oid) >= MAX_OID_LENGTH)
            return SNMPStatus::InvalidOID;
        OIDType name(const_cast<char *>(oid));
        unsigned char wire[MAX_OID_LENGTH];
        int size = name.serialise(wire, sizeof(wire));
        if (size < 0 || !name.fromBuffer(wire, size))
            return SNMPStatus::InvalidOID;
        for (size_t c = 0; c < columns_; ++c)
            if (!strcmp(column_[c], name._value))
                return SNMPStatus::InvalidOID;
        char alternative[MAX_OID_LENGTH] = {};
        if (fallback)
        {
            if (strlen(fallback) >= MAX_OID_LENGTH)
                return SNMPStatus::InvalidOID;
            OIDType other(const_cast<char *>(fallback));
            int n = other.serialise(wire, sizeof(wire));
            if (n < 0 || !other.fromBuffer(wire, n))
                return SNMPStatus::InvalidOID;
            strcpy(alternative, other._value);
        }
        strcpy(fallback_[columns_], alternative);
        expected_[columns_] = expected;
        fallbackExpected_[columns_] = fallbackExpected;
        strcpy(column_[columns_++], name._value);
        return SNMPStatus::Success;
    }
    SNMPStatus start()
    {
        if (pending())
            return SNMPStatus::Busy;
        if (!columns_)
            return SNMPStatus::InvalidConfiguration;
        walk_.configure(column_[0]);
        SNMPStatus status = walk_.start();
        if (!status.ok())
            return status;
        // Release old payloads only after start succeeds. Otherwise a smaller
        // table (or a timeout) could retain invisible rows from the previous poll.
        for (size_t row = 0; row < count_; ++row)
            rows_[row] = SNMPTableRow<Columns, IndexCapacity>();
        count_ = 0;
        current_ = 0;
        completed_ = false;
        status_ = SNMPStatus::Pending;
        columnErrors_ = false;
        usingFallback_ = false;
        return SNMPStatus::Success;
    }
    void cancel()
    {
        if (pending())
            walk_.cancel();
    }
    bool pending() const
    {
        return status_.code() == SNMPStatus::Pending;
    }
    bool takeCompleted()
    {
        bool done = completed_;
        completed_ = false;
        return done;
    }
    SNMPStatus status() const
    {
        return status_;
    }
    size_t size() const
    {
        return count_;
    }
    const SNMPTableRow<Columns, IndexCapacity> &operator[](size_t row) const
    {
        return rows_[row];
    }
    const SNMPTableRow<Columns, IndexCapacity> *begin() const
    {
        return rows_;
    }
    const SNMPTableRow<Columns, IndexCapacity> *end() const
    {
        return rows_ + count_;
    }

private:
    static_assert(Rows > 0 && Columns > 0, "Table requires row and column capacities");
    static_assert(IndexCapacity > 1 && IndexCapacity <= MAX_OID_LENGTH, "Invalid index capacity");
    SNMPWalk<1> walk_;
    SNMPTableRow<Columns, IndexCapacity> rows_[Rows];
    char column_[Columns][MAX_OID_LENGTH] = {}, fallback_[Columns][MAX_OID_LENGTH] = {};
    ASN_TYPE expected_[Columns] = {}, fallbackExpected_[Columns] = {};
    size_t columns_ = 0, count_ = 0, current_ = 0;
    SNMPStatus status_;
    bool completed_ = false, columnErrors_ = false, usingFallback_ = false;
    static bool consume(const SNMPResult &value, void *context)
    {
        auto &table = *static_cast<SNMPTableRead *>(context);
        const char *index = value.oid +
                            strlen(table.usingFallback_ ? table.fallback_[table.current_]
                                                        : table.column_[table.current_]) +
                            1;
        if (strlen(index) >= IndexCapacity)
            return false; // Explicit CapacityExceeded; never truncate or merge indices.
        size_t row = 0;
        while (row < table.count_ && strcmp(table.rows_[row].index, index))
            ++row;
        if (row == table.count_)
        {
            if (row == Rows)
                return false;
            table.rows_[row] = SNMPTableRow<Columns, IndexCapacity>();
            strcpy(table.rows_[row].index, index);
            ++table.count_;
        }
        auto &cell = table.rows_[row].cells[table.current_];
        if (table.usingFallback_ && cell.ok())
            return true;
        cell.value = value.value;
        cell.status = value.status;
        ASN_TYPE expected = table.usingFallback_ ? table.fallbackExpected_[table.current_]
                                                 : table.expected_[table.current_];
        if (cell.ok() && expected != NULLTYPE && expected != cell.value.type)
            cell.status = SNMPStatus::TypeMismatch;
        return true;
    }
    static void advance(SNMPOperation &operation, void *context)
    {
        auto &table = *static_cast<SNMPTableRead *>(context);
        SNMPStatus status = operation.status();
        if (status.code() == SNMPStatus::Cancelled || status.code() == SNMPStatus::CapacityExceeded)
        {
            table.status_ = status;
            table.completed_ = true;
            return;
        }
        if (!table.usingFallback_ && table.fallback_[table.current_][0])
        {
            bool missing = table.count_ == 0 || !status.ok();
            for (size_t r = 0; r < table.count_; ++r)
                if (!table.rows_[r].cells[table.current_].ok())
                    missing = true;
            if (missing)
            {
                table.usingFallback_ = true;
                table.walk_.configure(table.fallback_[table.current_]);
                status = table.walk_.start();
                if (status.ok())
                    return;
                table.status_ = status;
                table.completed_ = true;
                return;
            }
        }
        table.usingFallback_ = false;
        if (!status.ok())
            table.columnErrors_ = true;
        if (++table.current_ < table.columns_)
        {
            table.walk_.configure(table.column_[table.current_]);
            status = table.walk_.start();
            if (status.ok())
                return;
            table.status_ = status;
            table.completed_ = true;
            return;
        }
        table.status_ = table.columnErrors_ ? SNMPStatus::Partial : SNMPStatus::Success;
        for (size_t r = 0; r < table.count_; ++r)
            for (size_t c = 0; c < table.columns_; ++c)
                if (!table.rows_[r].cells[c].ok())
                    table.status_ = SNMPStatus::Partial;
        table.completed_ = true;
    }
};

// Interface descriptions and traffic: prefer Counter64, filling unavailable cells
// from Counter32 columns. Each cell retains its actual SNMP type and width.
template <size_t Rows, size_t IndexCapacity = MAX_OID_LENGTH>
class SNMPInterfaceRead : public SNMPTableRead<Rows, 3, IndexCapacity>
{
public:
    explicit SNMPInterfaceRead(SNMPDevice &device, bool highCapacity = true)
        : SNMPTableRead<Rows, 3, IndexCapacity>(device)
    {
        this->addColumn(".1.3.6.1.2.1.2.2.1.2", STRING); // ifDescr
        this->addColumn(highCapacity ? ".1.3.6.1.2.1.31.1.1.1.6" : ".1.3.6.1.2.1.2.2.1.10",
                        highCapacity ? COUNTER64 : COUNTER32,
                        highCapacity ? ".1.3.6.1.2.1.2.2.1.10" : nullptr, COUNTER32);
        this->addColumn(highCapacity ? ".1.3.6.1.2.1.31.1.1.1.10" : ".1.3.6.1.2.1.2.2.1.16",
                        highCapacity ? COUNTER64 : COUNTER32,
                        highCapacity ? ".1.3.6.1.2.1.2.2.1.16" : nullptr, COUNTER32);
    }
};
#endif
