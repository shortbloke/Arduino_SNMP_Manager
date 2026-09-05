#ifndef SNMP_TABLE_H
#define SNMP_TABLE_H
#include "SNMPClient.h"
#include <cstring>

struct SNMPCell
{
    SNMPValue value;
    SNMPStatus status = SNMPStatus::Missing;
    /**
     * @return True only when this cell contains a successful value.
     */
    bool ok() const
    {
        return status.ok();
    }
};

/**
 * @brief One discovered index and its selected cells.
 * @tparam Columns Number of cells in each row.
 * @tparam IndexCapacity Bytes of complete index text including termination.
 */
template <size_t Columns, size_t IndexCapacity = MAX_OID_LENGTH> struct SNMPTableRow
{
    char index[IndexCapacity] = {}; // Entire suffix, including composite indices.
    SNMPCell cells[Columns];
    /**
     * @param column Zero-based selected column; must be less than Columns.
     * @return Borrowed cell; unchecked access. Check cell.ok() before reading its value.
     */
    const SNMPCell &operator[](size_t column) const
    {
        return cells[column];
    }
};

// Walk selected columns one at a time and join them by the complete index suffix.
// The table owns all rows; a single streaming walk supplies bounded working state.
/**
 * @brief Own a bounded selected-column table joined by complete index suffixes.
 * @tparam Rows Maximum retained rows, greater than zero.
 * @tparam Columns Maximum configured columns, greater than zero.
 * @tparam IndexCapacity Index text bytes including termination; 2..MAX_OID_LENGTH.
 */
template <size_t Rows, size_t Columns, size_t IndexCapacity = MAX_OID_LENGTH> class SNMPTableRead
{
public:
    /**
     * @brief Create a table using one reusable streaming walk; does not send requests.
     * @param device Borrowed peer that must outlive the table.
     * @note Rows bounds retained rows, Columns bounds selected columns, and IndexCapacity
     *  bounds index text bytes including termination. This owner cannot be copied or moved.
     */
    explicit SNMPTableRead(SNMPDevice &device) : walk_(device)
    {
        walk_.stream(consume, this);
        walk_.onComplete(advance, this);
    }
    SNMPTableRead(const SNMPTableRead &) = delete;
    SNMPTableRead &operator=(const SNMPTableRead &) = delete;
    /**
     * @brief Select a column and optional alternate column for unavailable cells.
     * @param oid Numeric column OID without an instance suffix; canonical text is copied.
     * @param expected Required type, or NULLTYPE to accept any supported type.
     * @param fallback Alternate column OID, copied; null disables fallback.
     * @param fallbackExpected Required alternate type, or NULLTYPE for any supported type.
     * @return Success, Busy, CapacityExceeded, or InvalidOID. Failure adds no column.
     */
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
    /**
     * @brief Start collecting selected columns, one at a time.
     * @return Success when scheduled, Busy, InvalidConfiguration for no columns, or the walk's
     *  configuration/scheduling error. An accepted start clears old rows; a rejected start
     * preserves them.
     * @note Call client.loop() and check takeCompleted()/status(); Success is not a completed read.
     */
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
    /**
     * @brief Stop a pending table with Cancelled, retaining collected rows.
     * @note No effect after completion; returns no value.
     */
    void cancel()
    {
        if (pending())
            walk_.cancel();
    }
    /**
     * @return True while column reads or fallbacks are still in progress.
     */
    bool pending() const
    {
        return status_.code() == SNMPStatus::Pending;
    }
    /**
     * @return True once for a completed/cancelled table. Consumes the event, not stored rows.
     */
    bool takeCompleted()
    {
        bool done = completed_;
        completed_ = false;
        return done;
    }
    /**
     * @return Current outcome; Partial means some cells/columns failed. CapacityExceeded
     *  can leave incomplete rows, so always check each cell before use.
     */
    SNMPStatus status() const
    {
        return status_;
    }
    /**
     * @return Number of discovered rows currently retained, not the configured row maximum.
     */
    size_t size() const
    {
        return count_;
    }
    /**
     * @param row Zero-based row position, not the device's index; must be less than size().
     * @return Borrowed row in discovery order; unchecked. Invalidated by accepted
     * restart/destruction.
     */
    const SNMPTableRow<Columns, IndexCapacity> &operator[](size_t row) const
    {
        return rows_[row];
    }
    /**
     * @return Borrowed pointer to the first retained row for iteration; compare with end().
     */
    const SNMPTableRow<Columns, IndexCapacity> *begin() const
    {
        return rows_;
    }
    /**
     * @return Borrowed one-past-last pointer; never dereference it. Equals begin() when empty.
     */
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
    // Join by the complete suffix after the column OID, not arrival position or
    // the final number alone. Columns can be sparse and indices can contain several
    // numbers. Missing cells must stay explicit rather than shifting into another row.
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
        // A fallback column fills gaps only; do not replace a valid Counter64
        // reading with a narrower Counter32 reading from a later request.
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
    // Reuse one streaming walk for successive columns instead of reserving one
    // operation per column. This bounds working memory, but means a table is not
    // an atomic snapshot: the device can change between column reads.
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
/**
 * @brief Three-column interface table with optional Counter64 preference.
 * @tparam Rows Maximum logical interfaces retained, not physical-port count.
 * @tparam IndexCapacity Complete row-index text bytes including termination.
 */
template <size_t Rows, size_t IndexCapacity = MAX_OID_LENGTH>
class SNMPInterfaceRead : public SNMPTableRead<Rows, 3, IndexCapacity>
{
public:
    /**
     * @brief Select interface descriptions and incoming/outgoing byte totals.
     * @param device Borrowed peer that must outlive this table.
     * @param highCapacity Prefer Counter64 with Counter32 fallback when true; false reads
     *  Counter32 directly (useful for v1). Column order is description, incoming, outgoing.
     * @note Rows counts logical interfaces, not physical ports. Values are totals, not rates.
     */
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
