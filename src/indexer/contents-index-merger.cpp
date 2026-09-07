# include "contents-index-merger.hpp"
# include "dynamic-entities.hpp"
# include "serialize-cache.hpp"
# include "../../compat.hpp"
# include <mtc/radix-tree.hpp>
# include <mtc/arena.hpp>
# include <stdexcept>

template<> inline
structo::indexer::SerializeCache<mtc::IByteStream, 0x10000>*
  Serialize( structo::indexer::SerializeCache<mtc::IByteStream, 0x10000>* o, const void* p, size_t l )
{
  return o != nullptr ? o->put( p, l ) : nullptr;
}

namespace structo {
namespace indexer {
namespace fusion {

  using IEntityIterator = IContentsIndex::IEntitiesList;
  using IRecordIterator = IContentsIndex::IContentsList;
  using IEntities       = IContentsIndex::IEntities;
  using EntityReference = IEntities::Reference;

  constexpr size_t    max_docids = 0x200;
  constexpr uint32_t  max_length = 1 * 0x400 * 0x400;

  class EntityIterator
  {
    mtc::api<IEntityIterator> iterator;
    mtc::api<const IEntity>   curValue;
    EntityId                  entityId;

  public:
    EntityIterator( const mtc::api<IContentsIndex>& contentsIndex ):
      iterator( contentsIndex->ListEntities( "" ) ),
      curValue( iterator->Curr() ),
      entityId( curValue != nullptr ? curValue->GetId() : EntityId() ) {}

  public:
    auto  operator -> () const -> mtc::api<const IEntity>
      {  return curValue;  }
    auto  Curr() -> const EntityId&
      {  return entityId;  }
    auto  Next() -> const EntityId&
      {  return entityId = (curValue = iterator->Next()) != nullptr ? curValue->GetId() : EntityId();  }

  };

  class LexemeIterator
  {
    mtc::api<IRecordIterator> iterator;
    std::string               curValue;

  public:
    LexemeIterator( const mtc::api<IContentsIndex>& contentsIndex ):
      iterator( contentsIndex->ListContents() ),
      curValue( iterator->Curr() ) {}

  public:
    auto  Curr() const -> const std::string&
      {  return curValue;  }
    auto  Next() -> const std::string&
      {  return curValue = iterator->Next();  }

  };

  struct MapEntities
  {
    mtc::api<IContentsIndex::IEntities> entityBlock;
    const std::vector<uint32_t>&        mapEntities;
    bool                                stableOrder;
    EntityReference*                    entryBuffer = nullptr;

    MapEntities( mtc::api<IContentsIndex::IEntities> block, const std::vector<uint32_t>& remap, bool fixed ):
      entityBlock( block ),
      mapEntities( remap ),
      stableOrder( fixed )
    {}
  };

  struct RadixLink
  {
    uint32_t  bkType;
    uint32_t  uCount;
    uint64_t  offset;
    uint64_t  blkLen;
    uint32_t  navLen;

  public:
    auto  GetBufLen() const
    {
      return ::GetBufLen( bkType )
           + ::GetBufLen( uCount )
           + ::GetBufLen( offset )
           + ::GetBufLen( blkLen )
           + ::GetBufLen( navLen );
    }
    template <class O>
    O*    Serialize( O* o ) const
    {
      return
        ::Serialize(
        ::Serialize(
        ::Serialize(
        ::Serialize(
        ::Serialize( o, bkType ), uCount ), offset ), blkLen ), navLen );
    }

  };

 /*
  * DocAnchor отправляет в точку, где надо вычитывать приращение документа с индексом
  * больше указанного, на нужное смещение.
  */
  struct DocAnchor
  {
    unsigned  lastId;
    uint64_t  offset;
  };

  struct BlockInfo
  {
    uint32_t  ucount;
    uint64_t  blkLen;
    uint32_t  navLen;
  };

  inline
  auto  SerializeWithData( char* buffer, uint32_t diffi, uint32_t cbent ) -> char*
  {
    return ::Serialize( ::Serialize( buffer, diffi ), cbent );
  }

  inline
  auto  SerializeZeroData( char* buffer, uint32_t diffi, uint32_t ) -> char*
  {
    return ::Serialize( buffer, diffi );
  }

  using GetEntities = std::function<unsigned( EntityReference*, unsigned )>;

  class EntityBlock
  {
    EntityReference entSet[0x80];
    char            entBuf[0x80 * 0x40];
    GetEntities     getEnt;
    unsigned        ncount = 0;
    unsigned        nindex = 0;

  public:
    EntityBlock( GetEntities load ): getEnt( load )
    {}
   /*
    * access current entity; call loader if no more entities
    */
    auto  Curr() -> const EntityReference*
    {
      if ( nindex == ncount )
      {
        if ( ncount = getEnt( entSet, (nindex = 0) + std::size( entSet ) ); ncount != 0 )
        {
          char*   entOut = entBuf;

          for ( auto p = entSet, e = entSet + ncount; p != e; ++p )
            if ( auto size = p->details.size(); size <= 0x40 )
            {
              p->details = { (char*)memcpy( entOut, p->details.data(), size ), size };
                entOut += size;
            }
        }
      }
      return nindex < ncount ? &entSet[nindex]: nullptr;
    }
   /*
    * moves pointer to the next record
    */
    void  Next()
    {
      if ( ++nindex > ncount )
        nindex = ncount;
    }
  };

 /*
  * Последовательный загрузчик по сколько-то перенумерованных элементов в поданный массив
  */
  class StableLoader
  {
    mtc::api<IEntities>           block;
    const std::vector<uint32_t>&  renum;
    uint32_t                      ilast = 0;

  public:
    StableLoader( mtc::api<IEntities> b, const std::vector<uint32_t>& m ):
      block( b ),
      renum( m )
    {}
    auto  operator()( EntityReference* buf, unsigned len ) -> unsigned
    {
      auto  beg = buf;
      auto  end = buf + len;

      while ( buf != end && ilast != uint32_t(-1) )
      {
        if ( (ilast = (*buf = block->Find( ilast + 1 )).uEntity) == uint32_t(-1) )
          break;
        if ( (buf->uEntity = renum.at( ilast )) != uint32_t(-1) )
          ++buf;
      }
      return buf - beg;
    }
  };

  class RandomLoader
  {
    EntityReference*  buffer;
    unsigned          offset = 0;
    unsigned          length = 0;

  public:
    RandomLoader( const RandomLoader& l ):
      buffer( l.buffer ),
      offset( l.offset ),
      length( l.length )
    {}
    RandomLoader( mtc::api<IEntities> block, const std::vector<uint32_t>& renum, EntityReference* arena ):
      buffer( arena )
    {
      uint32_t  nextId = 0;

      for ( offset = length = 0; (nextId = (buffer[length] = block->Find( nextId + 1 )).uEntity) != uint32_t(-1); )
        if ( (buffer[length].uEntity = renum.at( buffer[length].uEntity )) != uint32_t(-1) )
          ++length;

      std::sort( buffer, buffer + length, []( const EntityReference& a, const EntityReference& b )
        {  return a.uEntity < b.uEntity; } );
    }
    auto  operator()( EntityReference* buf, unsigned len ) -> unsigned
    {
      auto  beg = buf;
      auto  end = buf + len;

      while ( offset != length && beg != end )
        *beg++ = buffer[offset++];
      return beg - buf;
    }
  };

  template <char* (*SerializeEntity)(char*, uint32_t, uint32_t)>
  auto  MergeChains(
    mtc::api<mtc::IByteStream>      output,
    const std::vector<MapEntities>& blocks,
    std::function<void( uint32_t )> onSize = {} ) -> BlockInfo
  {
    auto      points = std::vector<DocAnchor>();
    auto      serial = SerializeCache<mtc::IByteStream, 0x10000>( output.ptr() );
    auto      loader = std::vector<EntityBlock>();
    uint64_t  length = 0;
    uint32_t  uOldId = 0;
    DocAnchor daPrev = { 0, 0 };
    auto      nitems = size_t(0);     // objects in section
    auto      cbPart = uint32_t(0);   // current sec length
    char      docbuf[0x40];
    size_t    doclen;
    uint32_t  ucount = 0;

    for ( auto& block: blocks )
    {
      if ( block.stableOrder )
        loader.emplace_back( StableLoader( block.entityBlock, block.mapEntities ) );
      else
        loader.emplace_back( RandomLoader( block.entityBlock, block.mapEntities, block.entryBuffer ) );
    }

    for ( ; ; )
    {
      auto    entity = (const EntityReference*)nullptr;
      size_t  selpos;

    // select lower entity
      for ( size_t i = 0; i != loader.size(); ++i )
        if ( auto next = loader[i].Curr(); next != nullptr )
          if ( entity == nullptr || next->uEntity < entity->uEntity )
            entity = next, selpos = i;

      if ( entity != nullptr )
      {
        auto  nbytes = entity->details.size();
        auto  diffId = entity->uEntity - uOldId - 1;

      // serialize next difference
        doclen = SerializeEntity( docbuf, diffId, nbytes ) - docbuf;
          ::Serialize( ::Serialize( serial.ptr(), docbuf, doclen ), entity->details.data(), nbytes );

        length += nbytes + doclen;
        cbPart += nbytes + doclen;

        uOldId = entity->uEntity;
          ++ucount;
        loader[selpos].Next();

        // check for navigation point needed:
        // * too many documents
        // * too long block
        if ( (++nitems % max_docids) == 0 || cbPart >= max_length )
        {
          if ( onSize != nullptr )
            onSize( cbPart );
          points.push_back( { uOldId, length } );
            nitems = 1;
            cbPart = 0;
        }
      } else break;
    }

  // write navigation block
    if ( onSize != nullptr && cbPart != 0 )
      onSize( cbPart );

    cbPart = 0;

    for ( auto& next: points )
    {
      doclen = ::Serialize( ::Serialize( docbuf,
        next.lastId - daPrev.lastId ),
        next.offset - daPrev.offset ) - docbuf;

      ::Serialize( serial.ptr(), docbuf, doclen );
        cbPart += doclen;
        daPrev = next;
    }

    if ( onSize != nullptr && cbPart != 0 )
      onSize( cbPart );

    return serial.end(), BlockInfo{ ucount, length, cbPart };
  }

  void  ContentsMerger::MergeEntities()
  {
    using Entity = dynamic::EntityTable<std::allocator<char>>::Entity;

    auto  iterators = std::vector<EntityIterator>();
    auto  selectSet = std::vector<size_t>( indices.size() );
    auto  entityStm = storage->Entities();
    auto  bundleStm = storage->Packages();
    auto  entity_id = uint32_t(1);

  // create iterators list
    for ( auto& next: indices )
      iterators.emplace_back( next.index );

  // set zero document
    if ( Entity( std::allocator<char>() ).Serialize( entityStm.ptr() ) == nullptr )
      throw std::runtime_error( "Failed to serialize entities" );

    for ( ; ; )
    {
      auto  nCount = size_t(0);
      auto  iFresh = size_t(-1);

    // find entity with minimal id and select the one with bigger Version
      for ( size_t i = 0; i != iterators.size(); ++i )
      {
        if ( iterators[i].Curr().empty() )
          continue;

        if ( nCount == 0 || iterators[selectSet[nCount - 1]].Curr().compare( iterators[i].Curr() ) > 0 )
        {
          selectSet[(nCount = 1), 0] = iFresh = i;
        }
          else
        if ( iterators[selectSet[nCount - 1]].Curr().compare( iterators[i].Curr() ) == 0 )
        {
          selectSet[nCount++] = i;

          if ( iterators[i]->GetVersion() > iterators[iFresh]->GetVersion() )
            iFresh = i;
        }
      }

    // check if no more entities
      if ( nCount != 0 )
      {
        auto  bundlePos = int64_t(-1);
        auto  bundlePtr = mtc::api<const mtc::IByteBuffer>();

        if ( bundleStm != nullptr && (bundlePtr = iterators[iFresh]->GetBundle()) != nullptr )
          bundlePos = bundleStm->Put( bundlePtr->GetPtr(), bundlePtr->GetLen() );

        if ( Entity( std::allocator<char>() )
          .SetId( iterators[iFresh].Curr() )
          .SetIndex( entity_id )
          .SetExtra( make_view( iterators[iFresh]->GetExtra() ) )
          .SetPackPos( bundlePos )
          .SetVersion( iterators[iFresh]->GetVersion() ).Serialize( entityStm.ptr() ) == nullptr )
        {
          throw std::runtime_error( "Failed to serialize entities" );
        }

      // fill renumbering maps and request next documents
        for ( size_t i = 0; i != nCount; ++i )
        {
          if ( selectSet[i] == iFresh )
          {
            auto  ixPos = selectSet[i];
            auto& ixRec = indices[ixPos];
            auto  ixCur = iterators[ixPos]->GetIndex();

            ixRec.fixed &= (ixRec.oldId < ixCur);
            ixRec.remap[ixRec.oldId = ixCur] = entity_id++;
          }
            else
          indices[selectSet[i]].remap[iterators[selectSet[i]]->GetIndex()] = uint32_t(-1);

          iterators[selectSet[i]].Next();
        }
      } else break;
    }
    statMap["obj-count"] = uint32_t(entity_id);
  }

  void  ContentsMerger::MergeContents()
  {
    auto  contents  = storage->Contents();
    auto  chains    = storage->Linkages();
    auto  iterators = std::vector<LexemeIterator>();
    auto  selectSet = std::vector<size_t>( indices.size() );
    auto  radixTree = mtc::radix::sink<RadixLink>();
    auto  keyRecord = RadixLink{ 0, 0, 0, 0, 0 };
    auto  dynaBuffs = std::vector<EntityReference*>();
    auto  allocator = mtc::Arena();

  // create iterators list
    for ( auto& next : indices )
    {
      dynaBuffs.push_back( next.fixed ? nullptr :
        allocator.get_allocator<EntityReference>().allocate( next.index->GetMaxIndex() + 1 ) );
      iterators.emplace_back( next.index );
    }

  // list all the keys and select merge lists
    for ( ; ; )
    {
      auto  nCount = size_t(0);
      auto  select = (const std::string*)nullptr;

    // select lower key
      for ( size_t i = 0; i != iterators.size(); ++i )
        if ( !iterators[i].Curr().empty() )
        {
          int   rescmp;

          if ( nCount == 0 || (rescmp = select->compare( iterators[i].Curr() )) >= 0 )
          {
            if ( rescmp > 0 )
              nCount = 0;
            select = &iterators[selectSet[nCount++] = i].Curr();
          }
        }

    // check if key is available
      if ( nCount != 0 )
      {
        auto  blockList = std::vector<MapEntities>();
        auto  mergeStat = BlockInfo{};

        for ( size_t i = 0; i != nCount; ++i )
        {
          blockList.emplace_back(
            indices[selectSet[i]].index->GetKeyBlock( *select ),
            indices[selectSet[i]].remap,
            indices[selectSet[i]].fixed );
          blockList.back().entryBuffer = dynaBuffs[selectSet[i]];
        }

        mergeStat = blockList.front().entityBlock->Type() == 0 ?
          MergeChains<SerializeZeroData>( chains, blockList, onWriteSize ) :
          MergeChains<SerializeWithData>( chains, blockList, onWriteSize );

        if ( mergeStat.blkLen != 0 )
        {
          keyRecord.bkType = blockList.front().entityBlock->Type();
          keyRecord.uCount = mergeStat.ucount;
          keyRecord.blkLen = mergeStat.blkLen;
          keyRecord.navLen = mergeStat.navLen;

          radixTree.Insert( *select, keyRecord );

          keyRecord.offset += mergeStat.blkLen + mergeStat.navLen;
        }

        for ( size_t i = 0; i != nCount; ++i )
          iterators[selectSet[i]].Next();
      } else break;
    }

    radixTree.Serialize( contents.ptr() );

    statMap["key-count"] = uint32_t(radixTree.size());
    statMap["link-size"] = keyRecord.offset;
  }

  auto  ContentsMerger::Add( mtc::api<IContentsIndex> index ) -> ContentsMerger&
  {
    indices.emplace_back( index );
    return *this;
  }

  auto  ContentsMerger::Set( std::function<bool()> can ) -> ContentsMerger&
  {
    canContinue = can != nullptr ? can : [](){  return true;  };
    return *this;
  }

  auto  ContentsMerger::Set( std::function<void( uint32_t )> onSize ) -> ContentsMerger&
  {
    onWriteSize = onSize != nullptr ? onSize : []( uint32_t ){};
    return *this;
  }

  auto  ContentsMerger::Set( mtc::api<IStorage::IIndexStore> out ) -> ContentsMerger&
  {
    storage = out;
    return *this;
  }

  auto  ContentsMerger::Set( const mtc::api<IContentsIndex>* pi, size_t cc ) -> ContentsMerger&
  {
    for ( auto pe = pi + cc; pi != pe; ++pi )
      Add( *pi );
    return *this;
  }

  auto  ContentsMerger::Set( const std::vector<mtc::api<IContentsIndex>>& ix ) -> ContentsMerger&
  {
    for ( auto& next: ix )
      Add( next );
    return *this;
  }

  auto  ContentsMerger::Set( const std::initializer_list<const mtc::api<IContentsIndex>>& vx ) -> ContentsMerger&
  {
    for ( auto& next: vx )
      Add( next );
    return *this;
  }

  auto  ContentsMerger::operator()() -> mtc::api<IStorage::ISerialized>
  {
    auto  inputStat = statMap.set_array_zmap( "sources" );

  // check valid call
    if ( indices.size() == 0 )
      throw std::logic_error( "empty index list to be merged @" __FILE__ ":" LINE_STRING );

  // create iterators list
    for ( auto& next : indices )
    {
      auto  srcStats = next.index->Commit()->GetStats();
        srcStats.erase( "sources" );
      inputStat->push_back( srcStats );
    }

    MergeEntities();
    MergeContents();

    storage->SetStats( statMap );

    return storage->Commit();
  }

}}}
