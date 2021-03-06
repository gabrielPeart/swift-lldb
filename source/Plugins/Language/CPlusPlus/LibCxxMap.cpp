//===-- LibCxxList.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "LibCxx.h"

#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/Error.h"
#include "lldb/Core/Stream.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Host/Endian.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
    namespace formatters {
        class LibcxxStdMapSyntheticFrontEnd : public SyntheticChildrenFrontEnd
        {
        public:
            LibcxxStdMapSyntheticFrontEnd (lldb::ValueObjectSP valobj_sp);

            ~LibcxxStdMapSyntheticFrontEnd() override = default;

            size_t
            CalculateNumChildren() override;
            
            lldb::ValueObjectSP
            GetChildAtIndex(size_t idx) override;
            
            bool
            Update() override;
            
            bool
            MightHaveChildren() override;
            
            size_t
            GetIndexOfChildWithName(const ConstString &name) override;
            
        private:
            bool
            GetDataType();
            
            void
            GetValueOffset (const lldb::ValueObjectSP& node);
            
            ValueObject* m_tree;
            ValueObject* m_root_node;
            CompilerType m_element_type;
            uint32_t m_skip_size;
            size_t m_count;
            std::map<size_t,lldb::ValueObjectSP> m_children;
        };
    } // namespace formatters
} // namespace lldb_private

class MapEntry
{
public:
    MapEntry() = default;
    explicit MapEntry (ValueObjectSP entry_sp) : m_entry_sp(entry_sp) {}
    MapEntry (const MapEntry& rhs) : m_entry_sp(rhs.m_entry_sp) {}
    explicit MapEntry (ValueObject* entry) : m_entry_sp(entry ? entry->GetSP() : ValueObjectSP()) {}
    
    ValueObjectSP
    left () const
    {
        static ConstString g_left("__left_");
        if (!m_entry_sp)
            return m_entry_sp;
        return m_entry_sp->GetChildMemberWithName(g_left, true);
    }
    
    ValueObjectSP
    right () const
    {
        static ConstString g_right("__right_");
        if (!m_entry_sp)
            return m_entry_sp;
        return m_entry_sp->GetChildMemberWithName(g_right, true);
    }
    
    ValueObjectSP
    parent () const
    {
        static ConstString g_parent("__parent_");
        if (!m_entry_sp)
            return m_entry_sp;
        return m_entry_sp->GetChildMemberWithName(g_parent, true);
    }
    
    uint64_t
    value () const
    {
        if (!m_entry_sp)
            return 0;
        return m_entry_sp->GetValueAsUnsigned(0);
    }
    
    bool
    error () const
    {
        if (!m_entry_sp)
            return true;
        return m_entry_sp->GetError().Fail();
    }
    
    bool
    null() const
    {
        return (value() == 0);
    }
    
    ValueObjectSP
    GetEntry () const
    {
        return m_entry_sp;
    }
    
    void
    SetEntry (ValueObjectSP entry)
    {
        m_entry_sp = entry;
    }
    
    bool
    operator == (const MapEntry& rhs) const
    {
        return (rhs.m_entry_sp.get() == m_entry_sp.get());
    }
    
private:
    ValueObjectSP m_entry_sp;
};

class MapIterator
{
public:
    MapIterator() = default;
    MapIterator (MapEntry entry, size_t depth = 0) : m_entry(entry), m_max_depth(depth), m_error(false) {}
    MapIterator (ValueObjectSP entry, size_t depth = 0) : m_entry(entry), m_max_depth(depth), m_error(false) {}
    MapIterator (const MapIterator& rhs) : m_entry(rhs.m_entry),m_max_depth(rhs.m_max_depth), m_error(false) {}
    MapIterator (ValueObject* entry, size_t depth = 0) : m_entry(entry), m_max_depth(depth), m_error(false) {}
    
    ValueObjectSP
    value ()
    {
        return m_entry.GetEntry();
    }
    
    ValueObjectSP
    advance (size_t count)
    {
        ValueObjectSP fail(nullptr);
        if (m_error)
            return fail;
        size_t steps = 0;
        while (count > 0)
        {
            next();
            count--, steps++;
            if (m_error ||
                m_entry.null() ||
                (steps > m_max_depth))
                return fail;
        }
        return m_entry.GetEntry();
    }

protected:
    void
    next ()
    {
        if (m_entry.null())
            return;
        MapEntry right(m_entry.right());
        if (right.null() == false)
        {
            m_entry = tree_min(std::move(right));
            return;
        }
        size_t steps = 0;
        while (!is_left_child(m_entry))
        {
            if (m_entry.error())
            {
                m_error = true;
                return;
            }
            m_entry.SetEntry(m_entry.parent());
            steps++;
            if (steps > m_max_depth)
            {
                m_entry = MapEntry();
                return;
            }
        }
        m_entry = MapEntry(m_entry.parent());
    }

private:
    MapEntry
    tree_min (MapEntry&& x)
    {
        if (x.null())
            return MapEntry();
        MapEntry left(x.left());
        size_t steps = 0;
        while (left.null() == false)
        {
            if (left.error())
            {
                m_error = true;
                return MapEntry();
            }
            x = left;
            left.SetEntry(x.left());
            steps++;
            if (steps > m_max_depth)
                return MapEntry();
        }
        return x;
    }

    bool
    is_left_child (const MapEntry& x)
    {
        if (x.null())
            return false;
        MapEntry rhs(x.parent());
        rhs.SetEntry(rhs.left());
        return x.value() == rhs.value();
    }
    
    MapEntry m_entry;
    size_t m_max_depth;
    bool m_error;
};

lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::LibcxxStdMapSyntheticFrontEnd (lldb::ValueObjectSP valobj_sp) :
SyntheticChildrenFrontEnd(*valobj_sp.get()),
m_tree(NULL),
m_root_node(NULL),
m_element_type(),
m_skip_size(UINT32_MAX),
m_count(UINT32_MAX),
m_children()
{
    if (valobj_sp)
        Update();
}

size_t
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::CalculateNumChildren ()
{
    if (m_count != UINT32_MAX)
        return m_count;
    if (m_tree == NULL)
        return 0;
    ValueObjectSP m_item(m_tree->GetChildMemberWithName(ConstString("__pair3_"), true));
    if (!m_item)
        return 0;
    m_item = m_item->GetChildMemberWithName(ConstString("__first_"), true);
    if (!m_item)
        return 0;
    m_count = m_item->GetValueAsUnsigned(0);
    return m_count;
}

bool
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetDataType()
{
    if (m_element_type.GetOpaqueQualType() && m_element_type.GetTypeSystem())
        return true;
    m_element_type.Clear();
    ValueObjectSP deref;
    Error error;
    deref = m_root_node->Dereference(error);
    if (!deref || error.Fail())
        return false;
    deref = deref->GetChildMemberWithName(ConstString("__value_"), true);
    if (!deref)
        return false;
    m_element_type = deref->GetCompilerType();
    return true;
}

void
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetValueOffset (const lldb::ValueObjectSP& node)
{
    if (m_skip_size != UINT32_MAX)
        return;
    if (!node)
        return;
    CompilerType node_type(node->GetCompilerType());
    uint64_t bit_offset;
    if (node_type.GetIndexOfFieldWithName("__value_", NULL, &bit_offset) == UINT32_MAX)
        return;
    m_skip_size = bit_offset / 8u;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetChildAtIndex (size_t idx)
{
    static ConstString g___cc("__cc");
    static ConstString g___nc("__nc");

    
    if (idx >= CalculateNumChildren())
        return lldb::ValueObjectSP();
    if (m_tree == NULL || m_root_node == NULL)
        return lldb::ValueObjectSP();
    
    auto cached = m_children.find(idx);
    if (cached != m_children.end())
        return cached->second;
    
    bool need_to_skip = (idx > 0);
    MapIterator iterator(m_root_node, CalculateNumChildren());
    ValueObjectSP iterated_sp(iterator.advance(idx));
    if (iterated_sp.get() == NULL)
    {
        // this tree is garbage - stop
        m_tree = NULL; // this will stop all future searches until an Update() happens
        return iterated_sp;
    }
    if (GetDataType())
    {
        if (!need_to_skip)
        {
            Error error;
            iterated_sp = iterated_sp->Dereference(error);
            if (!iterated_sp || error.Fail())
            {
                m_tree = NULL;
                return lldb::ValueObjectSP();
            }
            GetValueOffset(iterated_sp);
            iterated_sp = iterated_sp->GetChildMemberWithName(ConstString("__value_"), true);
            if (!iterated_sp)
            {
                m_tree = NULL;
                return lldb::ValueObjectSP();
            }
        }
        else
        {
            // because of the way our debug info is made, we need to read item 0 first
            // so that we can cache information used to generate other elements
            if (m_skip_size == UINT32_MAX)
                GetChildAtIndex(0);
            if (m_skip_size == UINT32_MAX)
            {
                m_tree = NULL;
                return lldb::ValueObjectSP();
            }
            iterated_sp = iterated_sp->GetSyntheticChildAtOffset(m_skip_size, m_element_type, true);
            if (!iterated_sp)
            {
                m_tree = NULL;
                return lldb::ValueObjectSP();
            }
        }
    }
    else
    {
        m_tree = NULL;
        return lldb::ValueObjectSP();
    }
    // at this point we have a valid 
    // we need to copy current_sp into a new object otherwise we will end up with all items named __value_
    DataExtractor data;
    Error error;
    iterated_sp->GetData(data, error);
    if (error.Fail())
    {
        m_tree = NULL;
        return lldb::ValueObjectSP();
    }
    StreamString name;
    name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    auto potential_child_sp = CreateValueObjectFromData(name.GetData(), data, m_backend.GetExecutionContextRef(), m_element_type);
    if (potential_child_sp)
    {
        switch (potential_child_sp->GetNumChildren())
        {
            case 1:
            {
                auto child0_sp = potential_child_sp->GetChildAtIndex(0, true);
                if (child0_sp && child0_sp->GetName() == g___cc)
                    potential_child_sp = child0_sp;
                break;
            }
            case 2:
            {
                auto child0_sp = potential_child_sp->GetChildAtIndex(0, true);
                auto child1_sp = potential_child_sp->GetChildAtIndex(1, true);
                if (child0_sp && child0_sp->GetName() == g___cc &&
                    child1_sp && child1_sp->GetName() == g___nc)
                    potential_child_sp = child0_sp;
                break;
            }
        }
        potential_child_sp->SetName(ConstString(name.GetData()));
    }
    return (m_children[idx] = potential_child_sp);
}

bool
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::Update()
{
    m_count = UINT32_MAX;
    m_tree = m_root_node = NULL;
    m_children.clear();
    m_tree = m_backend.GetChildMemberWithName(ConstString("__tree_"), true).get();
    if (!m_tree)
        return false;
    m_root_node = m_tree->GetChildMemberWithName(ConstString("__begin_node_"), true).get();
    return false;
}

bool
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::MightHaveChildren ()
{
    return true;
}

size_t
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetIndexOfChildWithName (const ConstString &name)
{
    return ExtractIndexFromString(name.GetCString());
}

SyntheticChildrenFrontEnd*
lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator (CXXSyntheticChildren*, lldb::ValueObjectSP valobj_sp)
{
    if (!valobj_sp)
        return NULL;
    return (new LibcxxStdMapSyntheticFrontEnd(valobj_sp));
}
