# include "../../queries.hpp"

namespace structo {
namespace queries {

  auto  GetQuotation( const Abstract& abstract ) -> Abstract::Entries
  {
    return abstract.dwMode == Abstract::Rich ?
      abstract.entries : Abstract::Entries{ nullptr, nullptr };
  }

  auto  MakeAbstract( mtc::Arena& memory, const std::initializer_list<Abstract::EntrySet>& entset ) -> Abstract
  {
    auto      entryBuf = memory.get_allocator<Abstract::EntrySet>().allocate( entset.size() );
    Abstract  abstract{ Abstract::Rich, 1234, { entryBuf, entryBuf + entset.size() } };

    for ( auto& next: entset )
      new( entryBuf++ ) Abstract::EntrySet( next );

    return abstract;
  }

  auto  MakeEntrySet( mtc::Arena& memory, const std::initializer_list<Abstract::EntryPos>& posset, double weight ) -> Abstract::EntrySet
  {
    auto  entryPtr = memory.get_allocator<Abstract::EntryPos>().allocate( posset.size() );
    auto  entrySet = Abstract::EntrySet();

    entrySet.limits = { unsigned(-1), 0U };
    entrySet.weight = weight;
    entrySet.spread = { entryPtr, entryPtr + posset.size() };

    for ( auto& pos: posset )
    {
      entrySet.limits.uMin = std::min( entrySet.limits.uMin, pos.offset );
      entrySet.limits.uMax = std::max( entrySet.limits.uMax, pos.offset );
    }
    return entrySet;
  }

  auto  MakeEntrySet( mtc::Arena& memory, const std::initializer_list<unsigned>& posset, double weight ) -> Abstract::EntrySet
  {
    auto  entryPtr = memory.get_allocator<Abstract::EntryPos>().allocate( posset.size() );
    auto  entrySet = Abstract::EntrySet();

    entrySet.limits = { unsigned(-1), 0U };
    entrySet.weight = weight;
    entrySet.spread = { entryPtr, entryPtr + posset.size() };

    for ( auto& pos: posset )
    {
      entrySet.limits.uMin = std::min( entrySet.limits.uMin, pos );
      entrySet.limits.uMax = std::max( entrySet.limits.uMax, pos );
    }
    return entrySet;
  }

}}
