# include "../../queries/builder.hpp"
# include "../../context/pack-format.hpp"
# include "../../compat.hpp"
# include "rich-rankers.hpp"
# include "query-tools.hpp"
# include "field-set.hpp"
# include "decompressor.hpp"
# include "context/processor.hpp"
# include <mtc/bitset.h>

namespace structo {
namespace queries {

  using EntryPos = Abstract::EntryPos;
  using EntrySet = Abstract::EntrySet;

  using IEntities = IContentsIndex::IEntities;
  using Reference = IEntities::Reference;

 /*
  * RichQueryBase обеспечивает синхронное продвижение по форматам вместе с координатами
  * и передаёт распакованные форматы альтернативному методу доступа ко вхождениям.
  */
  struct RichQueryBase: IQuery
  {
   /*
    * This exception is thrown if a query copying results in uninitialized or empty query.
    *
    * Duplicate( bounds ) catches it and returns nullptr.
    */
    class uninitialized_exception: public std::runtime_error
      {  using runtime_error::runtime_error;  };

    RichQueryBase( mtc::api<IEntities> );
    RichQueryBase( const RichQueryBase& ) = delete;
    RichQueryBase( const RichQueryBase&, const Bounds& );

    // IQuery  overridables
    virtual auto  Duplicate( const Bounds& ) -> mtc::api<IQuery>;
    virtual auto  GetTuples( uint32_t ) -> const Abstract&;

    // local overridables
    virtual auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> = 0;
    virtual auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& = {} ) -> Abstract& = 0;

  protected:
    auto  SetPoints( EntryPos*, const EntryPos*, const EntrySet& ) const -> EntryPos*;

    mtc::api<IEntities>   fmtBlock;                           // formats and lengths
    IEntities::Reference  fmtRefer = { 0, { nullptr, 0 } };   // current formats position
    uint32_t              entityId = 0;                       // the found entity
    Abstract              abstract = {};
  };

 /*
  * RichQueryTerm - самый простой термин с одним координатным блоком, реализующий
  * поиск и ранжирование слова с одной лексемой
  */
  class RichQueryTerm final: public RichQueryBase
  {
    RichQueryTerm( const RichQueryTerm& ) = delete;
    RichQueryTerm( const RichQueryTerm&, const Bounds& );

  public:   // construction
    RichQueryTerm( mtc::api<IEntities>, mtc::api<IEntities>, const TermRanker& );

  // overridables
    auto  LastIndex() -> uint32_t override;
    auto  SearchDoc( uint32_t ) -> uint32_t override;
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control

  protected:
    mtc::api<IEntities> entBlock;
    const unsigned      datatype;
    TermRanker          tmRanker;
    Reference           docRefer = { 0, {} };
    EntrySet            entryBuf[0x10000];

  };

 /*
  * RichMultiTerm - коллекция их нескольких омонимов терминов, реализованная для
  * некоторой оптимизации вычислений - чтобы меньше было виртуальных вызовов и
  * больше использовался процессорный кэш
  */
  class RichMultiTerm final: public RichQueryBase
  {
    struct KeyBlock
    {
      mtc::api<IEntities> entBlock;           // coordinates
      const unsigned      datatype;
      TermRanker          tmRanker;
      Reference           docRefer = { 0, { nullptr, 0 } };   // entity reference set
      PosFid              entryPos[0x10000];

    // construction
      KeyBlock( const mtc::api<IEntities>& bk, const TermRanker& tr ):
        entBlock( bk ),
        datatype( bk->Type() ),
        tmRanker( tr )  {}

    // methods
      auto  Unpack( const Limits& limits ) -> unsigned
      {
        return
          datatype == 20 ? UnpackWordPos( entryPos, docRefer.details, limits ) :
          datatype == 21 ? UnpackWordFid( entryPos, docRefer.details, limits ) :
          throw std::logic_error( "unknown block type @" __FILE__ ":" LINE_STRING );
      }
    };

    RichMultiTerm( const RichMultiTerm&, const Bounds& );

  public:   // construction
    RichMultiTerm( mtc::api<IEntities>, std::vector<std::pair<mtc::api<IEntities>, TermRanker>>& );

  // overridables
    auto  LastIndex() -> uint32_t override;
    auto  SearchDoc( uint32_t ) -> uint32_t override;
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control

  protected:
    std::vector<KeyBlock>   blockSet;
    std::vector<EntrySet>   entryBuf;

  };

  class RichQueryArgs: public RichQueryBase
  {
  public:
    RichQueryArgs( mtc::api<IEntities> );
    RichQueryArgs( const RichQueryArgs&, const Bounds&, bool exceptIfNULL );

    void   AddQueryNode( mtc::api<RichQueryBase>, double );
    auto   StrictSearch( uint32_t ) -> uint32_t;

  protected:
    struct SubQuery
    {
      mtc::api<RichQueryBase> subQuery;
      unsigned                keyOrder;
      double                  keyRange;
      double                  leastSum = 0.0;
      unsigned                docFound = 0;
      Abstract                abstract = {};

    // methods
      auto  SearchDoc( uint32_t id ) -> uint32_t
      {
        if ( docFound >= id )
          return docFound;
        return abstract = {}, docFound = subQuery->SearchDoc( id );
      }
      auto  GetChunks( uint32_t id, mtc::span<const char> ft, const Limits& lm ) -> const Abstract&
      {
        return abstract.dwMode == abstract.None && docFound == id ?
          abstract = subQuery->GetChunks( id, ft, lm ) : abstract;
      }
    };

  protected:
    std::vector<SubQuery> querySet;
    std::vector<EntrySet> entryBuf;
    std::vector<EntryPos> pointBuf;
    const EntrySet*       entryEnd;
    const EntryPos*       pointEnd;

  };

  class RichQueryForce: public RichQueryArgs
  {
    using RichQueryArgs::RichQueryArgs;

  public:   // overridables
    auto  LastIndex() -> uint32_t override;
    auto  SearchDoc( uint32_t ) -> uint32_t override;

  };

  class RichQueryAll final: public RichQueryForce
  {
    using RichQueryForce::RichQueryForce;

  public:   // overridables
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control

  };

  class RichQueryOrder final: public RichQueryForce
  {
    using RichQueryForce::RichQueryForce;

  public:   // overridables
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control

  };

  class RichQueryFuzzy final: public RichQueryArgs
  {
    using RichQueryArgs::RichQueryArgs;

    RichQueryFuzzy( const RichQueryFuzzy& source, const Bounds& bounds ):
      RichQueryArgs( source, bounds, false ), quorum( source.quorum )  {}

  public:   // construction
    RichQueryFuzzy( mtc::api<IEntities> fmt, double quo ): RichQueryArgs( fmt ),
      quorum( quo ) {}

  // overridables
    auto  LastIndex() -> uint32_t override;
    auto  SearchDoc( uint32_t ) -> uint32_t override;
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

  protected:
    double    DistRange( double distance ) const
    {
      return distance >= 0 ?
        0.01 + 0.99 * cos(atan(fabs(distance / 5.0))) :
        0.01 + 0.95 * cos(atan(fabs(distance / 5.0)));
    }

    implement_lifetime_control

  protected:
    double  quorum;

  };

  class RichQueryAny final: public RichQueryArgs
  {
    using RichQueryArgs::RichQueryArgs;

  public:   // IQuery overridables
    auto  LastIndex() -> uint32_t override;
    auto  SearchDoc( uint32_t ) -> uint32_t override;
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control

  protected:
    std::vector<Abstract*>  selected;
  };

  class RichQueryNot final: public RichQueryArgs
  {
    using RichQueryArgs::RichQueryArgs;

  public:   // overridables
    auto  LastIndex() -> uint32_t override;
    auto  SearchDoc( uint32_t ) -> uint32_t override;
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control

  };

  class RichQueryField: public RichQueryBase
  {
  public:
  // construction
    RichQueryField( mtc::api<IEntities>, const std::vector<unsigned>&, mtc::api<RichQueryBase> );
    RichQueryField( const RichQueryField&, const Bounds& );

  // overridables
    auto  LastIndex() -> uint32_t override;
    auto  SearchDoc( uint32_t ) -> uint32_t override;

  protected:
    mtc::api<RichQueryBase> subQuery;
    std::vector<unsigned>   matchSet;
    EntrySet                entryBuf[0x10000];

  };

  class RichQueryCover final: public RichQueryField
  {
    using RichQueryField::RichQueryField;

  public:   // overridables
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control
  };

  class RichQueryMatch final: public RichQueryField
  {
    using RichQueryField::RichQueryField;

  public:   // overridables
    auto  BuildCopy( const Bounds& ) -> mtc::api<RichQueryBase> override;
    auto  GetChunks( uint32_t, mtc::span<const char>, const Limits& ) -> Abstract& override;

    implement_lifetime_control
  };

  // RichQueryBase implementation

  RichQueryBase::RichQueryBase( mtc::api<IEntities> fmt ): fmtBlock( fmt )
  {
  }

  RichQueryBase::RichQueryBase( const RichQueryBase& src, const Bounds& bounds )
  {
    if ( src.fmtBlock != nullptr && (fmtBlock = src.fmtBlock->Copy( bounds )) == nullptr )
      throw uninitialized_exception( "empty query element" );
  }

  auto  RichQueryBase::GetTuples( uint32_t tofind ) -> const Abstract&
  {
    // reposition the formats block to document ranked
    if ( tofind == entityId && (fmtRefer = fmtBlock->Find( tofind )).uEntity == tofind )
    {
      uint32_t  nWords;
      auto      fmtbeg = ::FetchFrom( fmtRefer.details.data(), nWords );

      abstract = GetChunks( tofind, { fmtbeg, size_t(fmtRefer.details.data() + fmtRefer.details.size() - fmtbeg) } );
        abstract.nWords = nWords;
      return abstract;
    }
    return abstract = {};
  }

  auto  RichQueryBase::Duplicate( const Bounds& bounds ) -> mtc::api<IQuery>
  {
    return BuildCopy( bounds ).ptr();
  }

  inline
  auto  RichQueryBase::SetPoints( EntryPos* out, const EntryPos* lim, const EntrySet& ent ) const -> EntryPos*
  {
    for ( auto beg = ent.spread.pbeg; out != lim && beg != ent.spread.pend; )
      *out++ = *beg++;
    return out;
  }

  // RichQueryTerm implementation

  RichQueryTerm::RichQueryTerm( const RichQueryTerm& rt, const Bounds& bounds ):
    RichQueryBase( rt, bounds ),
      entBlock( rt.entBlock->Copy( bounds ) ),
      datatype( rt.datatype ),
      tmRanker( rt.tmRanker )
  {
  }

  RichQueryTerm::RichQueryTerm( mtc::api<IEntities> ft, mtc::api<IEntities> bk, const TermRanker& tr ):
    RichQueryBase( ft ),
      entBlock( bk ),
      datatype( bk->Type() ),
      tmRanker( tr )
  {
  }

  auto  RichQueryTerm::LastIndex() -> uint32_t
  {
    return entBlock->Last();
  }

  auto  RichQueryTerm::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryTerm( *this, bounds );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

 /*
  * Search next document in the list of entities
  */
  uint32_t  RichQueryTerm::SearchDoc( uint32_t tofind )
  {
    if ( (tofind = std::max( entityId, tofind )) == uint32_t(-1) )
      return entityId = uint32_t(-1);

    if ( entityId >= tofind )
      return entityId;

    return abstract = {}, entityId = (docRefer = entBlock->Find( tofind )).uEntity;
  }

 /*
  * For changed document id, unpack && return the entries and entry sets for given
  * format set
  */
  Abstract& RichQueryTerm::GetChunks( uint32_t tofind, mtc::span<const char> fmt, const Limits& lim )
  {
    if ( docRefer.uEntity == tofind && abstract.dwMode == Abstract::None )
    {
      auto  format = context::formats::FormatBox( fmt );
      auto  ranker = [&]( unsigned pos, uint8_t fid ) -> double
        {  return tmRanker( format.Get( pos ), fid );  };
      auto  numEnt = datatype == 20 ?
        UnpackWordPos( entryBuf, docRefer.details, ranker, lim, 0 ) :
        UnpackWordFid( entryBuf, docRefer.details, ranker, lim, 0 );

      if ( numEnt != 0 )
        abstract = { Abstract::Rich, 0, entryBuf, entryBuf + numEnt };
    }
    return abstract;
  }

  // RichMultiTerm implementation

  RichMultiTerm::RichMultiTerm( const RichMultiTerm& multi, const Bounds& bounds ):
    RichQueryBase( multi, bounds ), entryBuf( 0x10000 )
  {
    for ( auto& next: multi.blockSet )
    {
      auto  blcopy = next.entBlock->Copy( bounds );

      if ( blcopy != nullptr )
        blockSet.emplace_back( blcopy, next.tmRanker );
    }
    if ( blockSet.empty() )
      throw uninitialized_exception( "RichMultiTerm::blockSet is empty @" __FILE__ LINE_STRING );
  }

  RichMultiTerm::RichMultiTerm( mtc::api<IEntities> fmt, std::vector<std::pair<mtc::api<IEntities>, TermRanker>>& terms ):
    RichQueryBase( fmt ),
    entryBuf( 0x10000 )
  {
    for ( auto& create: terms )
      blockSet.emplace_back( create.first, create.second );
  }

  auto  RichMultiTerm::LastIndex() -> uint32_t
  {
    auto  lastId = uint32_t(0);

    for ( auto block: blockSet )
      lastId = std::max( lastId, block.entBlock->Last() );

    return lastId;
  }

  uint32_t  RichMultiTerm::SearchDoc( uint32_t tofind )
  {
    uint32_t  uFound = uint32_t(-1);;

    if ( (tofind = std::max( tofind, entityId )) == uint32_t(-1) )
      return entityId = uint32_t(-1);

    if ( entityId >= tofind )
      return entityId;

    for ( auto& next: blockSet )
    {
      if ( next.docRefer.uEntity < tofind )
        next.docRefer = next.entBlock->Find( tofind );
      uFound = std::min( uFound, next.docRefer.uEntity );
    }

    return abstract = {}, entityId = uFound;
  }

  auto  RichMultiTerm::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichMultiTerm( *this, bounds );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

  Abstract& RichMultiTerm::GetChunks( uint32_t getdoc, mtc::span<const char> ftbuff, const Limits& limits )
  {
    struct  PosSet
    {
      PosFid*           ptrbeg;
      PosFid*           ptrend;
      const TermRanker& ranker;
    };

    if ( abstract.dwMode == Abstract::None )
    {
      auto      format = context::formats::FormatBox( ftbuff );
      auto      pfound = (PosSet*)alloca( blockSet.size() * sizeof(PosSet) );
      size_t    nfound = 0;
      auto      entPtr = entryBuf.data();
      auto      entEnd = entryBuf.data() + entryBuf.size();
      unsigned  lLimit;
      double    weight;

    // запросить распаковать все термы узла в массивы элементов { P, F }
    // сформировать записи для тех, которые присутствуют
      for ( auto& next: blockSet )
        if ( next.docRefer.uEntity == getdoc )
          if ( (lLimit = next.Unpack( limits )) != 0 )
            new( pfound + nfound++ ) PosSet{ next.entryPos, next.entryPos + lLimit, next.tmRanker };

    // check if single or multiple blocks
      if ( nfound == 0 )
        return abstract = {};

    // check if only one list
      if ( nfound == 1 )
      {
        auto&   ranker = pfound->ranker;

        for ( auto beg = pfound->ptrbeg, end = pfound->ptrend; beg != end && entPtr < entEnd; ++beg )
          if ( (weight = ranker( format.Get( beg->pos ), beg->fid )) > 0 )
            MakeEntrySet( *entPtr++, { beg->pos, 0 /* term_id */ }, weight );

        return entPtr != entryBuf.data() ? abstract = { Abstract::Rich, 0, { entryBuf.data(), entPtr } }
          : abstract = {};
      }

    // list multiterm data selecting minimal entries
      for ( lLimit = 0; entPtr != entEnd; )
      {
        auto      plower = decltype(pfound)( nullptr );
        unsigned  ulower = unsigned(-1);

        for ( auto next = pfound; next != pfound + nfound; ++next )
        {
          auto  clower = unsigned(-1);
          bool  hasOne;

        // пропустить все элементы, что уже добавлены в выдачу
          while ( (hasOne = next->ptrbeg != next->ptrend) && (clower = next->ptrbeg->pos) < lLimit )
            ++next->ptrbeg;

          if ( hasOne && (plower == nullptr || clower < ulower) )
          {
            plower = next;
            ulower = clower;
          }
        }

        if ( plower == nullptr )
          break;

        if ( (weight = plower->ranker( format.Get( ulower ), plower->ptrbeg->fid )) > 0 )
          MakeEntrySet( *entPtr++, { ulower, 0/* term_id */ }, weight );

        ++plower->ptrbeg, lLimit = ulower + 1;
      }
      return abstract = { Abstract::Rich, 0, { entryBuf.data(), entPtr } };
    }
    return getdoc == entityId ? abstract : abstract = {};
  }

  // RichQueryArgs implementation

  RichQueryArgs::RichQueryArgs( mtc::api<IEntities> fmt ):
    RichQueryBase( fmt ),
    entryBuf( 0x10000 ),
    pointBuf( 0x10000 ),
    entryEnd( entryBuf.data() + entryBuf.size() ),
    pointEnd( pointBuf.data() + pointBuf.size() )
  {
  }

  RichQueryArgs::RichQueryArgs( const RichQueryArgs& source, const Bounds& bounds, bool exceptIfNULL ):
    RichQueryBase( source, bounds ),
    entryBuf( 0x10000 ),
    pointBuf( 0x10000 ),
    entryEnd( entryBuf.data() + entryBuf.size() ),
    pointEnd( pointBuf.data() + pointBuf.size() )
  {
    for ( auto& next: source.querySet )
    {
      auto  newOne = next;

      if ( (newOne.subQuery = next.subQuery->BuildCopy( bounds )) == nullptr )
      {
        if ( exceptIfNULL )
          throw uninitialized_exception( "empty query @" __FILE__ LINE_STRING );
      }
        else
      querySet.emplace_back( newOne );
    }
    if ( querySet.empty() )
      throw uninitialized_exception( "empty query @" __FILE__ LINE_STRING );
  }

  void  RichQueryArgs::AddQueryNode( mtc::api<RichQueryBase> query, double range )
  {
    double  rgsumm = 0.0;

    querySet.push_back( { query, unsigned(querySet.size()), range } );

    std::sort( querySet.begin(), querySet.end(), []( const SubQuery& s1, const SubQuery& s2 )
      {  return s1.keyRange > s2.keyRange;  } );

    for ( auto& next : querySet )
      rgsumm += next.keyRange;

    for ( auto& next : querySet )
      next.leastSum = (rgsumm -= next.keyRange);
  }

  uint32_t  RichQueryArgs::StrictSearch( uint32_t tofind )
  {
    if ( (tofind = std::max( tofind, entityId )) == uint32_t(-1) )
      return entityId = uint32_t(-1);

    if ( entityId != tofind )
      abstract = {};

    for ( size_t nstart = 0; ; )
    {
      auto  nfound = querySet[nstart].SearchDoc( tofind );

      if ( nfound == uint32_t(-1) )
        return entityId = uint32_t(-1);

      if ( nfound == tofind )
      {
        if ( ++nstart >= querySet.size() )
          return entityId = tofind;
      } else nstart = (nstart == 0) ? 1 : 0;

      tofind = nfound;
    }
  }

  // RichQueryForce implementation

  auto  RichQueryForce::LastIndex() -> uint32_t
  {
    auto  uindex = uint32_t(-1);

    for ( auto& next: querySet )
      uindex = std::min( uindex, next.subQuery->LastIndex() );

    return uindex;
  }

  uint32_t  RichQueryForce::SearchDoc( uint32_t id )
  {
    return StrictSearch( id );
  }

  // RichQueryAll implementation

  auto  RichQueryAll::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryAll( *this, bounds, true );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

 /*
  * RichQueryAll::GetChunks( ... )
  *
  * Creates best ranked corteges of entries for '&' operator with no additional
  * checks for context except the checks provided by nested elements
  */
  Abstract& RichQueryAll::GetChunks( uint32_t udocid, mtc::span<const char> format, const Limits& limits )
  {
    if ( abstract.dwMode != abstract.Rich )
    {
      auto  outEnt = entryBuf.data();
      auto  outPos = pointBuf.data();

    // request all the queries in '&' operator; not found queries force to return {}
      for ( auto& next: querySet )
        if ( next.GetChunks( udocid, format, limits ).entries.empty() )
          return abstract = {};

    // list elements and select the best tuples for each possible compact entry;
    // shrink overlapping entries to suppress far and low-weight entries
      for ( bool hasAny = true; hasAny && outEnt != entryEnd && outPos != pointEnd; )
      {
        auto  limits = Limits{ unsigned(-1), 0 };
        auto  weight = 1.0;
        auto  outOrg = outPos;

      // find element to be the lowest in the list of entries
        for ( auto& next: querySet )
        {
          weight *= (1.0 -
            next.abstract.entries.pbeg->weight);
          limits.uLower = std::min( limits.uLower,
            next.abstract.entries.pbeg->limits.uMin );
          limits.uUpper = std::max( limits.uUpper,
            next.abstract.entries.pbeg->limits.uMax );
        }

      // finish weight calc
        weight = 1.0 - weight;

      // check if new or intersects with previously created element
        if ( outEnt != entryBuf.data() && outEnt[-1].limits.uMax >= limits.uLower && outEnt[-1].weight < weight )
          outOrg = outPos = (EntryPos*)(--outEnt)->spread.pbeg;

        for ( auto& next: querySet )
        {
        // copy relevant entries until possible overflow
          if ( (outPos = SetPoints( outPos, pointEnd, *next.abstract.entries.pbeg )) == pointEnd )
            return abstract = { Abstract::Rich, 0, { entryBuf.data(), outEnt } };

        // skip lower elements
          if ( next.abstract.entries.pbeg->limits.uMin == limits.uLower )
            hasAny &= ++next.abstract.entries.pbeg != next.abstract.entries.pend;
        }

        *outEnt++ = { { limits.uLower, limits.uUpper }, weight, { outOrg, outPos } };
      }
      abstract = { Abstract::Rich, 0, { entryBuf.data(), outEnt } };
    }
    return abstract;
  }

  // RichQueryOrder implementation

  auto  RichQueryOrder::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryOrder( *this, bounds, true );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

  Abstract& RichQueryOrder::GetChunks( uint32_t udocid, mtc::span<const char> format, const Limits& limits )
  {
    if ( abstract.dwMode != abstract.Rich )
    {
      auto  outEnt = entryBuf.data();
      auto  outPos = pointBuf.data();

    // request all the queries in '&' operator; not found queries force to return {}
      for ( auto& next: querySet )
        if ( next.GetChunks( udocid, format, limits ).entries.empty() )
          return abstract = {};

    // list elements and select the best tuples for each possible compact entry;
    // shrink overlapping entries to suppress far and low-weight entries
      while ( outEnt != entryEnd )
      {
        auto  begpos = unsigned(0);
        auto  weight = double{};
        auto  uLower = unsigned(-1);

        // search exact sequence of elements
        for ( size_t nindex = 0; nindex != querySet.size(); )
        {
          auto& next = querySet[nindex];
          auto& rent = next.abstract.entries;
          auto  cpos = unsigned{};

        // search first entry in next list at desired position or upper
          while ( rent.pbeg < rent.pend && (cpos = rent.pbeg->limits.uMin) < begpos + next.keyOrder )
            ++rent.pbeg;

        // check if found
          if ( rent.empty() )
          {
            if ( outEnt != entryBuf.data() )
              return abstract = { Abstract::Rich, 0, { entryBuf.data(), outEnt } };
            return abstract = {};
          }

        // get next position
          if ( nindex++ == 0 )
          {
            weight = 1.0 - rent.pbeg->weight;
              uLower = cpos;
            begpos = cpos - next.keyOrder;
          }
            else
          if ( cpos == begpos + next.keyOrder)
          {
            weight *= 1.0 - rent.pbeg->weight;
          }
            else
          {
            begpos = cpos - next.keyOrder;
            nindex = 0;
          }
          uLower = std::min( uLower, begpos );
        }

        // register key entries
        for ( auto& next: querySet )
        {
          *outPos++ = *next.abstract.entries.pbeg->spread.pbeg;
            ++next.abstract.entries.pbeg;
          if ( outPos == pointEnd )
            return abstract = { Abstract::Rich, 0, { entryBuf.data(), outEnt } };
        }

        *outEnt++ = { { uLower, unsigned(uLower + querySet.size() - 1) }, 1.0 - weight,
          { outPos - querySet.size(), outPos } };
      }
      abstract = { Abstract::Rich, 0, { entryBuf.data(), outEnt } };
    }
    return abstract;
  }

  // RichQueryFuzzy implementation

 /*
  * Но вообще-то последний надо вычислять как последний-из-тех-что-могут-дать-кворум
  */
  uint32_t  RichQueryFuzzy::LastIndex()
  {
    auto  lastId = uint32_t(0);

    for ( auto& next: querySet )
      lastId = std::max( lastId, next.subQuery->LastIndex() );

    return lastId;
  }

  uint32_t  RichQueryFuzzy::SearchDoc( uint32_t tofind )
  {
    double*   whlist = (double*)alloca( querySet.size() * sizeof(double) );
    double    weight = 0.0;
    uint32_t  ufound = uint32_t(-1);

    if ( (tofind = std::max( std::max( tofind, entityId ), 1U )) == uint32_t(-1) )
      return uint32_t(-1);

    abstract = {};

    for ( size_t nstart = 0; nstart != querySet.size(); )
    {
      auto&    rquery = querySet[nstart];
      uint32_t nextId = rquery.SearchDoc( tofind );

      whlist[nstart] = weight;

    // если очередной элемент не найден...
      if ( nextId == uint32_t(-1) || nextId > ufound )
      {
      // определяет ли он дальнейший кворум?
        if ( weight + rquery.leastSum >= quorum )
        {
          ++nstart;
        }
          else
        if ( ufound == uint32_t(-1) )
        {
          return entityId = uint32_t(-1);
        }
          else
        {
          weight = whlist[nstart = 0];
            tofind = ufound + 1;
          ufound = uint32_t(-1);
        }
      }
        else
      if ( nextId < ufound )
      {
        if ( rquery.keyRange + rquery.leastSum >= quorum )
        {
          weight = rquery.keyRange;
          ++nstart;
          ufound = nextId;
        }
          else
        if ( weight + rquery.leastSum >= quorum ) ++nstart;
          else
        tofind = ufound;
      }
        else
      {
        weight += rquery.keyRange;
        ++nstart;
      }
    }
    if ( weight < quorum )
      return entityId = uint32_t(-1);

    for ( auto& next: querySet )
      next.SearchDoc( ufound );

    return entityId = ufound;
  }

  auto  RichQueryFuzzy::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryFuzzy( *this, bounds );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

  Abstract& RichQueryFuzzy::GetChunks( uint32_t  udocid, mtc::span<const char> format, const Limits& limits )
  {
    if ( abstract.dwMode != abstract.Rich )
    {
      auto  outEnt = entryBuf.data();
      auto  outPos = pointBuf.data();
      auto  loaded = size_t(0);
      auto  quo_fl = double(0.0);
      auto  qUpper = unsigned(0);   // верхний предел для кворумных элементов

    // для начала загрузить потенциально дающие кворум подзапросы, чтобы избежать
    // распаковки потенциально ненужных
      for ( ; loaded != querySet.size() && quo_fl < quorum; ++loaded )
        if ( querySet[loaded].GetChunks( udocid, format, limits ).dwMode == abstract.Rich )
        {
          quo_fl += querySet[loaded].keyRange;
          qUpper = std::max( qUpper, querySet[loaded].abstract.entries.back().limits.uMax + 100 );
        }

    // list elements and select the best tuples for each possible compact entry;
    // shrink overlapping entries to suppress far and low-weight entries
      while ( outEnt != entryEnd && outPos != pointEnd )
      {
        auto      pLower = (Abstract::Entries*)nullptr;
        unsigned  uLower;
        unsigned  uUpper;
        auto      scalar = double(0.0);
        auto      en_len = double(0.0);
        auto      outOrg = outPos;
        auto      nquery = size_t(0);
        int       despos;                 // desired position

        // search exact sequence of elements
        for ( quo_fl = 0.0; nquery != querySet.size() && quo_fl < quorum; ++nquery )
        {
          auto& rentry = querySet[nquery].abstract.entries;

        // check if has the entries; use entries weight in possible quorum
          if ( rentry.pbeg == rentry.pend )
            continue;

          quo_fl += querySet[nquery].keyRange;

          if ( pLower == nullptr )
          {
            uLower = rentry.pbeg->limits.uMin;
            uUpper = rentry.pbeg->limits.uMax;
            despos = uLower - querySet[nquery].keyOrder;
            pLower = &rentry;
            scalar = rentry.pbeg->weight;
            en_len = rentry.pbeg->weight * rentry.pbeg->weight;
          }
            else
          {
            auto  fnDist = DistRange( int(rentry.pbeg->limits.uMin - despos - querySet[nquery].keyOrder) );
            auto  tmRank = rentry.pbeg->weight;

            if ( rentry.pbeg->limits.uMin < uLower )
              uLower = (pLower = &rentry)->pbeg->limits.uMin;
            if ( rentry.pbeg->limits.uMax > uUpper )
              uUpper = rentry.pbeg->limits.uMax;
            scalar += tmRank * fnDist;
            en_len += tmRank * tmRank;
          }
        }

      // check if element is found
        if ( quo_fl < quorum || pLower == nullptr )
          break;

      // подгрузить оставшиеся элементы
        for ( auto qLower = uLower > 30 ? uLower - 30 : 0; nquery != querySet.size(); ++nquery )
        {
          auto& rentry = querySet[nquery].abstract.entries;

        // check if loaded
          if ( nquery >= loaded )
          {
            querySet[nquery].GetChunks( udocid, format, { qLower, qUpper } );
            loaded = nquery + 1;
          }

        // обрабатываем только неисчерпанные
          if ( rentry.pbeg == rentry.pend )
            continue;

        // максимально приблизиться с найденному кворуму кортежа
          if ( rentry.pbeg[0].limits.uMin < uLower )
          {
            for ( auto plimit = rentry.pend - 1; rentry.pbeg < plimit && rentry.pbeg[1].limits.uMin < uLower;
              ++rentry.pbeg ) (void)NULL;
          }

        // проверить, не слишком ли далеко умотали
          if ( rentry.pbeg[0].limits.uMax > uLower + 60 )
            continue;

        // проверить возможные лимиты
          auto  fnDist = DistRange( int(rentry.pbeg->limits.uMin - despos - querySet[nquery].keyOrder) );
          auto  tmRank = rentry.pbeg->weight;

          if ( rentry.pbeg->limits.uMin < uLower )
            uLower = (pLower = &rentry)->pbeg->limits.uMin;
          scalar += tmRank * fnDist;
          en_len += tmRank * tmRank;
        }

        auto  weight = scalar / pow(querySet.size() * en_len, 0.25);
        bool  useEnt = weight >= 1e-3;

        if ( useEnt && outEnt != entryBuf.data() && outEnt[-1].limits.uMax >= uLower )
        {
          if ( (useEnt = (outEnt[-1].weight < weight)) == true )
            outOrg = outPos = (EntryPos*)(--outEnt)->spread.pbeg;
        }

        // skip to the next possible tuple
        if ( useEnt )
        {
          auto  uUpper = unsigned(0);

          for ( auto& rquery: querySet )
            if ( rquery.abstract.entries.pbeg != rquery.abstract.entries.pend )
            {
              outPos = SetPoints( outPos, pointEnd, *rquery.abstract.entries.pbeg );
              uUpper = std::max( uUpper, rquery.abstract.entries.pbeg->limits.uMax );
            }

          *outEnt++ = { { uLower, uUpper }, weight, { outOrg, outPos } };
        }

        ++pLower->pbeg;
      }
      abstract = { outEnt != entryBuf.data() ? Abstract::Rich : Abstract::None, 0,
        { entryBuf.data(), outEnt } };
    }
    return abstract;
  }

  // RichQueryAny implementation

  auto  RichQueryAny::LastIndex() -> uint32_t
  {
    auto  uindex = uint32_t(0);

    for ( auto& next: querySet )
      uindex = std::max( uindex, next.subQuery->LastIndex() );

    return uindex;
  }

  uint32_t  RichQueryAny::SearchDoc( uint32_t tofind )
  {
    uint32_t  uFound;

    if ( (tofind = std::max( tofind, entityId )) == uint32_t(-1) )
      return entityId = uint32_t(-1);

    if ( entityId != tofind )
      abstract = {};

    uFound = uint32_t(-1);

    for ( auto& next: querySet )
      uFound = std::min( uFound, next.SearchDoc( tofind ) );

    return entityId = uFound;
  }

  auto  RichQueryAny::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryAny( *this, bounds, false );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

  Abstract& RichQueryAny::GetChunks( uint32_t udocid, mtc::span<const char> format, const Limits& limits )
  {
    if ( abstract.dwMode != abstract.Rich )
    {
      auto  outEnt = entryBuf.data();
      auto  nFound = size_t(0);

    // ensure selected allocated
      if ( selected.size() != querySet.size() )
        selected.resize( querySet.size() );

    // request all the queries in '&' operator; not found queries force to return {}
      for ( auto& next: querySet )
        if ( next.GetChunks( udocid, format, limits ).entries.size() != 0 )
          selected[nFound++] = &next.abstract;

    // list elements and select the best tuples for each possible compact entry;
    // shrink overlapping entries to suppress far and low-weight entries
      while ( outEnt != entryEnd )
      {
        auto      plower = (Abstract*)nullptr;
        unsigned  ulower;

      // find element to be the lowest in the list of entries
        for ( size_t i = 0; i != nFound; ++i )
          if ( selected[i]->entries.size() != 0 )
          {
            auto& next = selected[i]->entries;

          // select lower element
            if ( plower != nullptr )
            {
              int  rcmp = (next.pbeg->limits.uMin > plower->entries.pbeg->limits.uMin)
                        - (next.pbeg->limits.uMin < plower->entries.pbeg->limits.uMin);

              if ( rcmp < 0 || (rcmp == 0 && next.pbeg->weight > plower->entries.pbeg->weight) )
                plower = selected[i];
            } else plower = selected[i];
          }

      // check if found
        if ( plower == nullptr )
          return abstract = { Abstract::Rich, 0, { entryBuf.data(), outEnt } };

      // create output entry
        ulower = (*outEnt++ = *plower->entries.pbeg++).limits.uMin;

      // skip equal entries
        for ( size_t i = 0; i != nFound; ++i )
          if ( selected[i]->entries.size() != 0 && selected[i]->entries.pbeg->limits.uMin == ulower )
            ++selected[i]->entries.pbeg;
      }
      abstract = { Abstract::Rich, 0, { entryBuf.data(), outEnt } };
    }
    return abstract;
  }

// RichQueryNot implementation

  auto  RichQueryNot::LastIndex() -> uint32_t
  {
    return querySet.front().subQuery->LastIndex();
  }

  auto  RichQueryNot::SearchDoc( uint32_t tofind ) -> uint32_t
  {
    for ( abstract = {}; entityId < tofind; ++tofind )
    {
      auto  next = querySet.begin();

      if ( (tofind = querySet.front().SearchDoc( tofind )) == uint32_t(-1) )
        return entityId = uint32_t(-1);

      for ( ++next; next != querySet.end(); ++next )
        if ( next->SearchDoc( tofind ) == tofind )
          break;

      if ( next == querySet.end() )
        return entityId = tofind;
    }
    return entityId;
  }

  auto  RichQueryNot::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryNot( *this, bounds, true );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

  auto  RichQueryNot::GetChunks( uint32_t entity, mtc::span<const char> markup, const Limits& limits ) -> Abstract&
  {
    if ( entityId == entity && abstract.dwMode == Abstract::None )
      abstract = querySet.front().GetChunks( entity, markup, limits );
    return abstract;
  }

// RichQueryField implementation

  RichQueryField::RichQueryField(
    mtc::api<IEntities>           fmt,
    const std::vector<unsigned>&  fds,
    mtc::api<RichQueryBase>       sub ): RichQueryBase( fmt ),
      subQuery( sub )
  {
    for ( auto id: fds )
      mtc::bitset_set( matchSet, id );
  }

  RichQueryField::RichQueryField( const RichQueryField& source, const Bounds& bounds ):
    RichQueryBase( source, bounds ), matchSet( source.matchSet )
  {
    if ( source.subQuery != nullptr )
      subQuery = source.subQuery->BuildCopy( bounds );
  }

  auto  RichQueryField::LastIndex() -> uint32_t
  {
    return subQuery->LastIndex();
  }

  auto  RichQueryField::SearchDoc( uint32_t id ) -> uint32_t
  {
    return entityId < id ? abstract = {},
      entityId = subQuery->SearchDoc( id ) : entityId;
  }

  // RichQueryCover implementation

  auto  RichQueryCover::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryCover( *this, bounds );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

  Abstract& RichQueryCover::GetChunks( uint32_t id, mtc::span<const char> ft, const Limits& limits )
  {
    if ( id == entityId && abstract.dwMode != abstract.Rich )
    {
      auto  format = context::formats::FormatBox( ft );
      auto  fmtbeg = format.begin();
      auto  fmtend = format.end();
      auto  outPtr = entryBuf;
      bool  hasAny;

    // skip until the first matching format
      while ( (hasAny = fmtbeg != fmtend) && !mtc::bitset_get( matchSet, fmtbeg->format ) )
        ++fmtbeg;

    // request entry chunks
      if ( hasAny )
      {
        auto  entset = subQuery->GetChunks( id, ft, { std::max( limits.uLower, fmtbeg->uLower ), limits.uUpper  } );

        for ( ; entset.entries.pbeg != entset.entries.pend && fmtbeg != fmtend; ++entset.entries.pbeg )
        {
          while ( fmtbeg != fmtend && fmtbeg->uUpper < entset.entries.pbeg->limits.uMax && !mtc::bitset_get( matchSet, fmtbeg->format ) )
            ++fmtbeg;

          if ( fmtbeg == fmtend )
            break;

          while ( entset.entries.pbeg != entset.entries.pend && entset.entries.pbeg->limits.uMin < fmtbeg->uLower )
            ++entset.entries.pbeg;

          if ( entset.entries.pbeg == entset.entries.pend )
            break;

          if ( fmtbeg->uLower <= entset.entries.pbeg->limits.uMin
            && fmtbeg->uUpper >= entset.entries.pbeg->limits.uMax )
          {
            *outPtr++ = *entset.entries.pbeg;
          }
        }
      }

      if ( outPtr != entryBuf )
        abstract = { Abstract::Rich, 0, { entryBuf, outPtr } };
    }
    return abstract;
  }

  // RichQueryMatch implementation

  auto  RichQueryMatch::BuildCopy( const Bounds& bounds ) -> mtc::api<RichQueryBase>
  {
    try
      {  return new RichQueryMatch( *this, bounds );  }
    catch ( const uninitialized_exception& )
      {  return nullptr;  }
  }

  Abstract& RichQueryMatch::GetChunks( uint32_t id, mtc::span<const char> ft, const Limits& limits )
  {
    if ( id == entityId && abstract.dwMode != abstract.Rich )
    {
      auto  format = context::formats::FormatBox( ft );
      auto  fmtbeg = format.begin();
      auto  fmtend = format.end();
      auto  outPtr = entryBuf;
      bool  hasAny;

      // skip until the first matching format
      while ( (hasAny = fmtbeg != fmtend) && !mtc::bitset_get( matchSet, fmtbeg->format ) )
        ++fmtbeg;

      // request entry chunks
      if ( hasAny )
      {
        auto  entset = subQuery->GetChunks( id, ft, { std::max( limits.uLower, fmtbeg->uLower ), limits.uUpper  } );

        for ( ; entset.entries.pbeg != entset.entries.pend && fmtbeg != fmtend; ++entset.entries.pbeg )
        {
          while ( fmtbeg != fmtend && fmtbeg->uUpper < entset.entries.pbeg->limits.uMax && !mtc::bitset_get( matchSet, fmtbeg->format ) )
            ++fmtbeg;

          if ( fmtbeg == fmtend )
            break;

          while ( entset.entries.pbeg != entset.entries.pend && entset.entries.pbeg->limits.uMin < fmtbeg->uLower )
            ++entset.entries.pbeg;

          if ( entset.entries.pbeg == entset.entries.pend )
            break;

          if ( fmtbeg->uLower == entset.entries.pbeg->limits.uMin
            && fmtbeg->uUpper == entset.entries.pbeg->limits.uMax )
          {
            *outPtr++ = *entset.entries.pbeg;
          }
        }
      }

      if ( outPtr != entryBuf )
        abstract = { Abstract::Rich, 0, { entryBuf, outPtr } };
    }
    return abstract;
  }

  // Query creation entry

  class RichBuilder
  {
    const mtc::zmap&                terms;
    const mtc::zmap                 zstat;
    const uint32_t                  total;
    const mtc::api<IContentsIndex>& index;
    const context::Processor&       lproc;
    const FieldHandler&             fdhan;
    mtc::api<IEntities>             mkups;

    struct QuerySettings
    {
      FieldSet  fieldSet;
      unsigned  uContext = unsigned(-1);
      bool      isStrict = false;

      auto  SetStrict( bool strict ) const -> QuerySettings
        {  return strict == isStrict ? *this : QuerySettings{ fieldSet, uContext, strict };  }
    };

  protected:
    struct SubQuery
    {
      mtc::api<RichQueryBase> query;
      double                  range;
    };

  public:
    RichBuilder( const mtc::zmap& tm, const mtc::api<IContentsIndex>& dx, const context::Processor& lp, const FieldHandler& fs ):
      terms( tm ),
      zstat( tm.get_zmap( "terms-range-map", {} ) ),
      total( terms.get_word32( "collection-size", 0 ) ),
      index( dx ),
      lproc( lp ),
      fdhan( fs ),
      mkups( index->GetKeyBlock( "fmt" ) )  {}

    auto  BuildQuery( const mtc::zval&, const QuerySettings& ) const -> SubQuery;
    auto  CreateWord( const mtc::widestr&, const QuerySettings& ) const -> SubQuery;
  template <bool Forced, class Output>
    auto  CreateArgs( mtc::api<Output>,
      const mtc::array_zval&, const QuerySettings& ) const -> SubQuery;
    auto  CreateExcl(
      const mtc::array_zval&, const QuerySettings& ) const -> SubQuery;
    auto  AsWildcard( const mtc::widestr&, const QuerySettings& ) const -> SubQuery;
    auto  GetTermIdf( const mtc::widestr& ) const -> double;
    auto  GetTermIdf( const context::Key& ) const -> double;
    auto  LoadFields( const mtc::zval& ) const -> FieldSet;
  };

  auto  RichBuilder::BuildQuery( const mtc::zval& query, const QuerySettings& sets ) const -> SubQuery
  {
    auto  op = GetOperator( query );

    if ( op == "word" )
      return CreateWord( op.GetString(), sets );
    if ( op == "wildcard" )
      return AsWildcard( op.GetString(), sets );
    if ( op == "!" )
      return CreateExcl( op.GetVector(), sets );
    if ( op == "&&" )
      return CreateArgs<true> ( mtc::api( new RichQueryAll( mkups ) ), op.GetVector(), sets );
    if ( op == "||" )
      return CreateArgs<false>( mtc::api( new RichQueryAny( mkups ) ), op.GetVector(), sets );
    if ( op == "quote" || op == "order" )
    {
      return CreateArgs<true> ( mtc::api( new RichQueryOrder( mkups ) ), op.GetVector(),
        sets.SetStrict( op == "quote" ) );
    }
    if ( op == "fuzzy" )
    {
      auto  subset = std::vector<SubQuery>{};
      auto  quorum = 0.0;
      auto  addone = SubQuery{};
      auto  pquery = mtc::api<RichQueryFuzzy>();

    // create subquery list
      for ( auto& next: op.GetVector() )
        if ( (addone = BuildQuery( next, sets )).query != nullptr )
        {
          subset.push_back( addone );
          quorum += addone.range;
        }

    // create and fill the query
      pquery = new RichQueryFuzzy( mkups, 0.7 * quorum );

      for ( auto& next: subset )
        pquery->AddQueryNode( next.query, next.range );

      return { pquery.ptr(), 0.7 * quorum };
    }
    if ( op == "cover" || op == "match" )
    {
      auto  pnames = op.GetStruct().get( "field" );
      auto  pquery = op.GetStruct().get( "query" );
      auto  fields = FieldSet();
      auto  squery = SubQuery();

      if ( pnames != nullptr )  fields = std::move( LoadFields( *pnames ) );
        else throw std::invalid_argument( "missing 'field' as field limitation" );

      if ( pquery != nullptr )  squery = BuildQuery( *pquery, sets );
        else throw std::invalid_argument( "missing 'query' as limited object" );

      return op == "match" ?
        SubQuery{ new RichQueryMatch( mkups, fields, squery.query ), squery.range } :
        SubQuery{ new RichQueryCover( mkups, fields, squery.query ), squery.range };
    }
    throw std::logic_error( "operator not supported" );
  }

  auto  RichBuilder::CreateWord( const mtc::widestr& str, const QuerySettings& sets ) const -> SubQuery
  {
    auto  lexemes = lproc.Lemmatize( str );
    auto  ablocks = std::vector<std::pair<mtc::api<IEntities>, TermRanker>>();
    auto  fWeight = GetTermIdf( str );
    auto  pkblock = mtc::api<IEntities>();

  // check for statistics is present
    if ( fWeight <= 0.0 )
      return { nullptr, 0.0 };

  // request terms for the word
    for ( auto& lexeme: lexemes )
      if ( (pkblock = index->GetKeyBlock( { lexeme.data(), lexeme.size() } )) != nullptr )
        ablocks.emplace_back( pkblock, TermRanker( sets.fieldSet, lexeme, fWeight, sets.isStrict ) );

  // if nothing found, return nullptr
    if ( ablocks.size() == 0 )
      return { nullptr, 0.0 };

    if ( ablocks.size() != 1 )
      return { new RichMultiTerm( mkups, ablocks ), fWeight };

    return { new RichQueryTerm( mkups, ablocks.front().first, std::move( ablocks.front().second ) ), fWeight };
  }

  template <bool Forced, class Output>
  auto  RichBuilder::CreateArgs( mtc::api<Output> to, const mtc::array_zval& args, const QuerySettings& fds ) const -> SubQuery
  {
    std::vector<SubQuery> queries;
    SubQuery              created;
    double                fWeight;

    if ( Forced )
    {
      fWeight = 0.0;

      for ( auto& next: args )
        if ( (created = BuildQuery( next, fds )).query != nullptr )
        {
          fWeight = std::max( fWeight, created.range );
          queries.push_back( std::move( created ) );
        } else return { nullptr, 0.0 };
    }
      else
    {
      fWeight = 1.0;

      for ( auto& next: args )
        if ( (created = BuildQuery( next, fds )).query != nullptr )
        {
          fWeight = std::min( fWeight, created.range );
          queries.push_back( std::move( created ) );
        }
    }

    if ( queries.empty() )
      return { nullptr, 0.0 };

    if ( queries.size() == 1 )
      return queries.front();

    for ( auto& next: queries )
      to->AddQueryNode( next.query, next.range );

    return { to.ptr(), fWeight };
  }

  auto  RichBuilder::CreateExcl( const mtc::array_zval& args, const QuerySettings& fds ) const -> SubQuery
  {
    auto  queries = std::vector<SubQuery>();
    auto  created = SubQuery();
    auto  exclude = mtc::api( new RichQueryNot( mkups ) );

    for ( size_t i = 0; i != args.size(); ++i )
    {
      if ( (created = BuildQuery( args[i], fds )).query == nullptr )
      {
        if ( i == 0 )
          return { nullptr, 0.0 };
        continue;
      }
      queries.push_back( std::move( created ) );
    }

    if ( queries.empty() )
      return { nullptr, 0.0 };

    if ( queries.size() == 1 )
      return queries.front();

    for ( auto& next: queries )
      exclude->AddQueryNode( next.query, next.range );

    return { exclude.ptr(), queries.front().range };
  }

  auto  RichBuilder::AsWildcard( const mtc::widestr& str, const QuerySettings& sets ) const -> SubQuery
  {
    auto  lexemes = lproc.Lemmatize( str, context::Processor::as_wildcard );
    auto  ablocks = std::vector<std::pair<mtc::api<IEntities>, TermRanker>>();
    auto  fWeight = GetTermIdf( widechar('{') + str + widechar('}') );
    auto  pkblock = mtc::api<IEntities>();
    auto  wildKey = context::Key( 0xff, codepages::strtolower( str ) );
    auto  keyList = index->ListContents( { wildKey.data(), wildKey.size() } );

    // check for statistics is present
    if ( fWeight <= 0.01 )    // check if placeholder
      return { nullptr, 0.0 };

    // create cooblocks
    for ( auto& next: lexemes )
      if ( (pkblock = index->GetKeyBlock( { next.data(), next.size() } )) != nullptr )
        ablocks.emplace_back( pkblock, TermRanker( sets.fieldSet, next, GetTermIdf( next ), true ) );

    // enrich lexemes with index terms
    for ( auto next = keyList->Curr(); next.size() != 0; next = keyList->Next() )
      if ( (pkblock = index->GetKeyBlock( next )) != nullptr )
        ablocks.emplace_back( pkblock, TermRanker( sets.fieldSet, next, GetTermIdf( next ), true ) );

    // if nothing found, return nullptr
    if ( ablocks.size() == 0 )
      return { nullptr, 0.0 };

    if ( ablocks.size() == 1 )
      return { new RichQueryTerm( mkups, ablocks.front().first, std::move( ablocks.front().second ) ), fWeight };

    return { new RichMultiTerm( mkups, ablocks ), fWeight };
  }

  auto  RichBuilder::GetTermIdf( const mtc::widestr& str ) const -> double
  {
    auto  pstats = zstat.get_zmap( str );
    auto  prange = pstats != nullptr ? pstats->get_double( "range" ) : nullptr;

    if ( pstats == nullptr )
      return -1.0;

    return prange != nullptr ? *prange : -1.0;
  }

  auto  RichBuilder::GetTermIdf( const context::Key& key ) const -> double
  {
    auto  kstats = index->GetKeyStats( { key.data(), key.size() } );

    if ( kstats.nCount == 0 || kstats.nCount > total )
      return -1.0;

    return log( (1.0 + total) / kstats.nCount ) / log(1.0 + total);
  }

  auto  RichBuilder::LoadFields( const mtc::zval& fds ) const -> FieldSet
  {
    auto  fields = FieldSet();

    switch ( fds.get_type() )
    {
      case mtc::zval::z_charstr:
        return { fdhan, *fds.get_charstr() };
      case mtc::zval::z_array_charstr:
        return { fdhan, *fds.get_array_charstr() };
      default:
        throw std::invalid_argument( "invalid 'field' format" );
    }
  }

  auto  BuildRichQuery(
    const mtc::zval&                query,
    const mtc::zmap&                terms,
    const mtc::api<IContentsIndex>& index,
    const context::Processor&       lproc,
    const FieldHandler&             fdset ) -> mtc::api<IQuery>
  {
    auto  zterms( terms );

    if ( zterms.empty() )
      zterms = RankQueryTerms( LoadQueryTerms( query ), index, lproc );

    return RichBuilder( zterms, index, lproc, fdset )
      .BuildQuery( query, { fdset } ).query.ptr();
  }

}}
