# include "index-layers.hpp"

#include <absl/strings/str_format.h>
#include <absl/strings/internal/str_format/extension.h>
#include <mtc/ptr.h>
#include <storage/posix-fs.hpp>

# include "override-entities.hpp"

namespace structo {
namespace indexer {

  class IndexLayers::Entities final: public IContentsIndex::IEntities
  {
    friend class IndexLayers;

    struct BlockEntry
    {
      uint32_t            uLower;
      uint32_t            uUpper;
      BanPtr              banned;
      mtc::api<IEntities> entSet;
    };

    using BlockSet = std::list<BlockEntry>;

  public:
    mtc::api<const Iface>             holder;
    BlockSet                          blocks;
    mutable BlockSet::const_iterator  pblock;
    uint32_t                          ncount = 0;
    uint32_t                          bktype = uint32_t(-1);

    implement_lifetime_control

  public:
    Entities( const Iface* parent = nullptr );

    void  AddBlock( const BlockEntry& );

    // overridables
    auto  Copy( const Bounds& ) const -> mtc::api<IEntities> override;
    auto  Find( uint32_t ) -> Reference override;
    auto  Last() const -> uint32_t override;
    auto  Size() const -> uint32_t override {  return ncount;  }
    auto  Type() const -> uint32_t override {  return bktype;  }

  };

  // IndexLayers implementation

  IndexLayers::IndexLayers( const mtc::api<IContentsIndex>* indices, size_t count )
  {
    uint32_t  uLower = 1;

    for ( auto end = indices + count; indices != end; uLower += (*indices++)->GetMaxIndex() )
      layers.store( new IndexEntry( uLower, *indices, layers.load() ) );
  }

 /*
  * getEntity( id )
  *
  * Returns entity from one of indices held which is not excluded from by other indices
  * with override process.
  *
  * The resulted entity has overriden index.
  */
  auto  IndexLayers::getEntity( EntityId id ) const -> mtc::api<const IEntity>
  {
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      if ( auto entity = layer->pIndex->GetEntity( id ); entity != nullptr )
        return layer->Override( entity );
    return {};
  }

  auto  IndexLayers::getEntity( uint32_t ix ) const -> mtc::api<const IEntity>
  {
    for ( auto layer = layers.load(); layer != nullptr && layer->uUpper >= ix; layer = layer->pChain.load() )
      if ( layer->uLower <= ix )
        return layer->Override( layer->pIndex->GetEntity( ix - layer->uLower + 1 ) );

    return {};
  }

  bool  IndexLayers::delEntity( EntityId id )
  {
    auto  deleted = false;

    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      deleted |= layer->pIndex->DelEntity( id );

    return deleted;
  }

  auto  IndexLayers::setExtras( EntityId id, const std::string_view& xtras ) -> mtc::api<const IEntity>
  {
    auto  entity = mtc::api<const IEntity>();

    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      if ( (entity = layer->pIndex->SetExtras( id, xtras )) != nullptr )
        return layer->Override( entity );

    return {};
  }

  auto  IndexLayers::getMaxIndex() const -> uint32_t
  {
    if ( auto layer = layers.load(); layer != nullptr )
      return layer->uLower + layer->pIndex->GetMaxIndex() - 1;
    return 0;
  }

  auto  IndexLayers::getKeyBlock( const std::string_view& key, const mtc::Iface* pix ) const -> mtc::api<IContentsIndex::IEntities>
  {
    auto  entities = mtc::api( new Entities( pix ) );

  // fill blocks to the entities holder
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      if ( auto block = layer->pIndex->GetKeyBlock( key ); block != nullptr )
        entities->AddBlock( { layer->uLower, layer->uUpper, layer->banned, block } );

  // check if blocks layers has only one block
    return entities->Size() != 0 ? entities.ptr() : nullptr;
  }

  auto  IndexLayers::getKeyStats( const std::string_view& key ) const -> IContentsIndex::BlockInfo
  {
    IContentsIndex::BlockInfo blockStats = { uint32_t(-1), 0 };

    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
    {
      auto  cStats = layer->pIndex->GetKeyStats( key );

      if ( cStats.bkType == uint32_t(-1) )
        continue;
      if ( blockStats.bkType == uint32_t(-1) )  blockStats = cStats;
        else
      if ( blockStats.bkType == cStats.bkType ) blockStats.nCount += cStats.nCount;
        else
      throw std::invalid_argument( "Block types differ in sequental indives" );
    }

    return blockStats;
  }

  void  IndexLayers::addContents( mtc::api<IContentsIndex> ix )
  {
    auto  layer = layers.load();
    auto  lower = layer != nullptr ? layer->uUpper + 1 : 1;
    auto  alloc = new IndexEntry( lower, ix, layer );

    while ( !layers.compare_exchange_strong( layer, alloc ) )
    {
      alloc->uLower = layer->uUpper + 1;
      alloc->pChain = layer;
    }
  }

  auto  IndexLayers::listContents( const std::string_view& key, const mtc::Iface* poo  ) -> mtc::api<IContentsIndex::IContentsList>
  {
    /*
    auto  contents = std::vector<mtc::api<IContentsIndex::IContentsList>>();
    auto  nextList = mtc::api<IContentsIndex::IContentsList>();

    for ( auto& next: layers )
      if ( (nextList = next.pIndex->ListContents( key )) != nullptr )
        contents.emplace_back( nextList );

    return contents.size() > 1 ? new ContentsList( contents, poo ) :
           contents.size() > 0 ? contents.front() : nullptr;
    */
    return nullptr;
  }

  void  IndexLayers::commitItems()
  {
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      layer->pIndex->Commit();
  }

 /*
  * Разрешить коллизии идентификаторов в загруженных индексах, выставив соответствующие биты
  * для документов, более свежие версии которых есть в начале списка
  */
  void  IndexLayers::hideClashes()
  {
    using Iterator = mtc::api<IContentsIndex::IEntitiesList>;
    using Document = mtc::api<const IEntity>;

    struct  Entry
    {
      IndexEntry* player;
      Iterator    itnext;
      Document    entity;
      EntityId    ent_id;
    };

    auto  itlist = std::vector<Entry>();
    auto  select = std::vector<std::vector<Entry>::iterator>();

  // построить массив в порядке возрастания индексов по времени
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
    {
      auto  itnext = layer->pIndex->ListEntities( "" );
      auto  entity = itnext != nullptr ? itnext->Curr() : nullptr;

      if ( entity != nullptr )
        itlist.push_back( { layer, itnext, entity, entity->GetId() } );
    }

    std::reverse(
      itlist.begin(), itlist.end() );
    select.resize(
      itlist.size() );

  // пройтись по всем идентификаторам и, если есть более чем в одном индексе, забанить в остальных
    while ( itlist.size() > 1 )
    {
      auto    sel_id = (const EntityId*)nullptr;
      size_t  sellen = 0;

    // select mininal ids
      for ( auto it = itlist.begin(); it != itlist.end(); ++it )
      {
        int   rescmp = sel_id == nullptr ? -1 : it->ent_id.compare( *sel_id );

      // select minimal entities
        if ( rescmp <= 0 )
          sel_id = &(select[(sellen = rescmp < 0 ? 0 : sellen)++] = it)->ent_id;
      }

    // check latest versions
      for ( size_t i = 0; i + 1 < sellen; ++i )
      {
        auto  uindex = select[i]->entity->GetIndex();

        (*select[i]->player->banned)[uindex / std::numeric_limits<uint32_t>::digits]
          |= (1 << (uindex % std::numeric_limits<uint32_t>::digits));
      }

      for ( size_t i = sellen; i-- > 0; )
      {
        if ( (select[i]->entity = select[i]->itnext->Next()) != nullptr )
          select[i]->ent_id = select[i]->entity->GetId();
        else itlist.erase( select[i] );
      }
    }
  }

  // IndexLayers::IndexEntry implementation

  IndexLayers::IndexEntry::IndexEntry( uint32_t lower, mtc::api<IContentsIndex> index, IndexEntry* chain ):
    pChain( chain ),
    uLower( lower ),
    uUpper( uLower + index->GetMaxIndex() - 1 ),
    pIndex( index ),
    banned( std::make_shared<BanMap>(
      (index->GetMaxIndex() + std::numeric_limits<uint32_t>::digits - 1) / std::numeric_limits<uint32_t>::digits ) )
  {
  }

  auto  IndexLayers::IndexEntry::Override( mtc::api<const IEntity> entity ) const -> mtc::api<const IEntity>
  {
    return uLower > 1 ? Override::Entity( entity ).Index(
      entity->GetIndex() + uLower - 1 ) : entity;
  }

  // IndexLayers::Entities implementation

  IndexLayers::Entities::Entities( const Iface* pix ):
    holder( pix ), pblock( blocks.begin() )
  {
  }

  void  IndexLayers::Entities::AddBlock( const BlockEntry& block )
  {
    if ( blocks.empty() )
      bktype = block.entSet->Type();

    blocks.push_front( block );
      pblock = blocks.begin();
      ncount += block.entSet->Size();
  }

  auto  IndexLayers::Entities::Find( uint32_t ix ) -> Reference
  {
    for ( auto  getRef = Reference(); pblock != blocks.end(); ++pblock )
    {
      if ( ix < pblock->uLower )
        ix = pblock->uLower;

      while ( pblock != blocks.end() && pblock->uUpper < ix )
        ++pblock;

      if ( pblock == blocks.end() )
        break;

      if ( (getRef = pblock->entSet->Find( ix - pblock->uLower + 1 )).uEntity != (uint32_t)-1 )
        return getRef.uEntity += pblock->uLower - 1, getRef;
    }
    return { uint32_t(-1), {} };
  }

  auto  IndexLayers::Entities::Last() const -> uint32_t
  {
    return blocks.size() != 0 ? blocks.back().uLower + blocks.back().entSet->Last() : 0;
  }

  auto  IndexLayers::Entities::Copy( const Bounds& bounds ) const -> mtc::api<IEntities>
  {
    auto  newent = mtc::api( new Entities( holder ) );

    newent->bktype = bktype;

  // add blocks with non-empty Entities after copying
    for ( auto& block: blocks )
      if ( block.uLower <= bounds.uUpper
        && block.uUpper >= bounds.uLower )
      {
        auto  blcopy = BlockEntry{
          block.uLower,
          block.uUpper,
          block.banned,
          block.entSet->Copy( { bounds.uLower - block.uLower + 1, bounds.uUpper - block.uLower + 1 } ) };

        if ( blcopy.entSet != nullptr )
          newent->AddBlock( blcopy );
    }

    return newent->blocks.size() != 0 ? newent.ptr() : nullptr;
  }

  // IndexLayers::ContentsList implementation

  IndexLayers::ContentsList::ContentsList( const std::vector<mtc::api<IContentsList>>& list, const Iface* parent ):
    parentObject( parent )
  {
    for ( auto& next: list )
      contentsList.push_back( next );

    for ( auto& next: contentsList )
      if ( next.Curr().size() != 0 )
        if ( currentValue == nullptr || next.Curr() < *currentValue )
          currentValue = &next.Curr();
  }

  auto  IndexLayers::ContentsList::Curr() -> std::string
  {
    return currentValue != nullptr ? *currentValue : std::string();
  }

  auto  IndexLayers::ContentsList::Next() -> std::string
  {
    if ( currentValue != nullptr )
    {
      const std::string*  minValue = nullptr;

      for ( auto& next: contentsList )
      {
        if ( currentValue == &next.Curr() || *currentValue == next.Curr() )
          next.Next();
        if ( next.Curr().size() != 0 )
          if ( minValue == nullptr || next.Curr() < *minValue )
            minValue = &next.Curr();
      }
      currentValue = minValue;
    }
    return currentValue != nullptr ? *currentValue : std::string();
  }

}}
