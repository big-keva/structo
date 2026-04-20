# include "../../indexer/static-contents.hpp"
# include "override-entities.hpp"
# include "static-entities.hpp"
# include "dynamic-bitmap.hpp"
# include "patch-table.hpp"
# include "strmatch.hpp"
# include <mtc/radix-tree.hpp>
# include <mtc/arena.hpp>

namespace structo {
namespace indexer {
namespace static_ {

  class ContentsIndex final: public IContentsIndex
  {
    using Allocator = mtc::Arena::allocator<char>;
    using ISerialized = IStorage::ISerialized;
    using IBlocksRepo = IStorage::ICoordsRepo;
    using IByteBuffer = mtc::IByteBuffer;
    using PatchHolder = PatchTable<Allocator>;
    using ContentsTable = mtc::radix::dump<const char>;
    using ContentsIterator = ContentsTable::const_iterator;

    class EntitiesBase;
    class EntitiesLite;
    class EntitiesRich;
    class EntityIterator;
    class LexemeIterator;
    class PatchApplier;

    implement_lifetime_control

  public:
    ContentsIndex( mtc::api<IStorage::ISerialized> storage );
   ~ContentsIndex() = default;

  public:
    auto  GetEntity( EntityId id ) const -> mtc::api<const IEntity> override;
    auto  GetEntity( uint32_t id ) const -> mtc::api<const IEntity> override;
    bool  DelEntity( EntityId ) override;
    auto  SetEntity( EntityId, const mtc::span<const EntryView>&,
      const std::string_view&, const std::string_view& ) -> mtc::api<const IEntity> override;
    auto  SetExtras( EntityId, const std::string_view& ) -> mtc::api<const IEntity> override;

    auto  GetMaxIndex() const -> uint32_t override
      {  return entities.GetEntityCount();  }

    auto  GetKeyBlock( const std::string_view& ) const -> mtc::api<IEntities> override;
    auto  GetKeyStats( const std::string_view& ) const -> BlockInfo override;

    auto  ListEntities( EntityId ) -> mtc::api<IEntitiesList> override;
    auto  ListEntities( uint32_t ) -> mtc::api<IEntitiesList> override;

    auto  ListContents( const std::string_view& ) -> mtc::api<IContentsList> override;

    auto  Commit() -> mtc::api<IStorage::ISerialized> override;
    auto  Reduce() -> mtc::api<IContentsIndex> override  {  return this;  }
    void  Remove() override;

  protected:
    bool  delEntity( EntityId, uint32_t );

  protected:
    mtc::Arena                  memArena;       // allocation arena
    mtc::api<ISerialized>       xStorage;       // serialized object storage holder
    mtc::api<const IByteBuffer> tableBuf;
    mtc::api<const IByteBuffer> radixBuf;
    EntityTable<Allocator>      entities;       // static entities table
    ContentsTable               contents;       // radix tree view
    mtc::api<IBlocksRepo>       blockBox;
    PatchHolder                 patchTab;
    Bitmap<Allocator>           shadowed;       // deleted documents identifiers

  };

  class ContentsIndex::EntitiesBase: public IEntities
  {
    struct DocDowel
    {
      uint32_t  lastId;
      uint64_t  offset;
    };
    struct DocIndex final: std::vector<DocDowel>, Iface
    {
      implement_lifetime_control
    };

  public:
    EntitiesBase( const mtc::api<const mtc::IByteBuffer>&, uint32_t, uint32_t, const ContentsIndex* );
    EntitiesBase( const EntitiesBase&, const Bounds& );

  // overridables
    auto  Last() const -> uint32_t override;
    auto  Size() const -> uint32_t override {  return ncount;  }
    auto  Type() const -> uint32_t override {  return bkType;  }

  protected:
    const uint32_t                    bkType;
    const uint32_t                    ncount;
    const Bounds                      limits;   // [min, max[
    mtc::api<const ContentsIndex>     parent;
    mtc::api<const mtc::IByteBuffer>  iblock;
    const char* const                 origin;
    const char*                       finish;
    const char*                       ptrtop;
    Reference                         curref = { 0, { nullptr, 0 } };
    mtc::api<DocIndex>                pindex;
    const DocDowel*                   dowBeg = nullptr;
    const DocDowel*                   dowEnd = nullptr;

  };

  class ContentsIndex::EntitiesLite final: public EntitiesBase
  {
    using EntitiesBase::EntitiesBase;

  public:
    EntitiesLite( const EntitiesLite& source, const Bounds& bounds ):
      EntitiesBase( source, bounds ) {}

    auto  Find( uint32_t ) -> Reference override;
    auto  Copy( const Bounds& ) const -> mtc::api<IEntities> override;

    implement_lifetime_control
  };

  class ContentsIndex::EntitiesRich final: public EntitiesBase
  {
    using EntitiesBase::EntitiesBase;

  public:
    EntitiesRich( const EntitiesRich& source, const Bounds& bounds ):
      EntitiesBase( source, bounds ) {}

    auto  Find( uint32_t ) -> Reference override;
    auto  Copy( const Bounds& ) const -> mtc::api<IEntities> override;

    implement_lifetime_control
  };

  class ContentsIndex::EntityIterator final: public IEntitiesList
  {
    implement_lifetime_control

  public:
    EntityIterator( EntityTable<Allocator>::Iterator&& it, ContentsIndex* pc ):
      contents( pc ),
      iterator( std::move( it ) ) {}

  public:
    auto  Curr() -> mtc::api<const IEntity> override;
    auto  Next() -> mtc::api<const IEntity> override;

  protected:
    mtc::api<ContentsIndex>           contents;
    EntityTable<Allocator>::Iterator  iterator;

  };

  class ContentsIndex::LexemeIterator final: public IContentsList
  {
    implement_lifetime_control

  public:
    LexemeIterator( ContentsIndex*, const std::string_view& );

  public:
    auto  Curr() -> std::string override;
    auto  Next() -> std::string override;

  protected:
    mtc::api<ContentsIndex> contents;
    ContentsIterator        iterator;
    std::string             templStr;

  };

  class ContentsIndex::PatchApplier: public IStorage::ISerialized::IPatch
  {
    ContentsIndex&  contents;

    void  Delete( EntityId ) override;
    void  Update( EntityId, const void*, size_t ) override;

  public:
    PatchApplier( ContentsIndex& ix ): contents( ix ) {}

    implement_lifetime_stub
  };

  // ContentsIndex implementation

  ContentsIndex::ContentsIndex( mtc::api<IStorage::ISerialized> storage ):
    xStorage( storage ),
    tableBuf( storage->Entities() ),
    radixBuf( storage->Contents() ),
    entities( make_view( tableBuf ), this, storage->Packages(), memArena.get_allocator<char>() ),
    contents( radixBuf->GetPtr() ),
    blockBox( storage->Linkages() ),
    patchTab( std::max( 1000U, entities.GetEntityCount() ), memArena.get_allocator<char>() ),
    shadowed( entities.GetEntityCount(), memArena.get_allocator<char>() )
  {
    PatchApplier  apatch( *this );

    storage->SetPatch( &apatch );
    patchTab.Freeze();
  }

  auto  ContentsIndex::GetEntity( EntityId id ) const -> mtc::api<const IEntity>
  {
    auto  entity = entities.GetEntity( id );

    if ( entity != nullptr && !shadowed.Get( entity->index ) )
    {
      auto  ppatch = patchTab.Search( { id.data(), id.size() } );

      if ( ppatch == nullptr )
        return Override::Entity( entity.ptr() ).Bundle( xStorage->Packages(), entity->GetPackPos() );

      if ( ppatch->GetLen() == size_t(-1) )
        return nullptr;

      return Override::Entity( Override::Entity( entity.ptr() )
        .Bundle( xStorage->Packages(), entity->GetPackPos() ) )
        .Extra( ppatch );
    }
    return nullptr;
  }

  auto  ContentsIndex::GetEntity( uint32_t id ) const -> mtc::api<const IEntity>
  {
    return !shadowed.Get( id ) ? entities.GetEntity( id ).ptr() : nullptr;
  }

 /*
  * DelEntity( EntityId id )
  *
  * Creates the patch record for the deleted document.
  */
  bool  ContentsIndex::DelEntity( EntityId id )
  {
    auto  getdoc = entities.GetEntity( id );

    if ( getdoc != nullptr )
    {
      auto  ppatch = patchTab.Search( { id.data(), id.size() } );

      if ( ppatch == nullptr || ppatch->GetLen() != size_t(-1) )
        return delEntity( id, getdoc->index );
    }
    return false;
  }

  auto  ContentsIndex::SetEntity( EntityId, const mtc::span<const EntryView>&,
    const std::string_view&, const std::string_view& ) -> mtc::api<const IEntity>
  {
    throw std::logic_error( "static_::ContentsIndex::SetEntity( ) must not be called" );
  }

  auto  ContentsIndex::SetExtras( EntityId id, const std::string_view& xtras ) -> mtc::api<const IEntity>
  {
    auto  getdoc = entities.GetEntity( id );

    if ( getdoc != nullptr && !shadowed.Get( getdoc->index ) )
    {
      auto  ppatch = patchTab.Update( { id.data(), id.size() }, getdoc->index, xtras );

      return ppatch == nullptr || ppatch->GetLen() != size_t(-1) ?
        Override::Entity( getdoc.ptr() ).Extra( ppatch ) : getdoc.ptr();
    }
    return nullptr;
  }

  auto  ContentsIndex::GetKeyBlock( const std::string_view& key ) const -> mtc::api<IEntities>
  {
    auto  pfound = contents.Search( { key.data(), key.size() } );

    if ( pfound != nullptr )
    {
      uint32_t  blockType;
      uint32_t  nEntities;
      uint64_t  blockOffs;
      uint64_t  blockSize;

      if ( ::FetchFrom( ::FetchFrom( ::FetchFrom( ::FetchFrom( pfound,
        blockType ),
        nEntities ),
        blockOffs ),
        blockSize ) != nullptr )
      {
        auto  pblock = mtc::api<const IByteBuffer>( blockBox->Get( blockOffs, blockSize ).ptr() );

        if ( blockType == 0 )
          return new EntitiesLite( pblock, blockType, nEntities, this );
        else
          return new EntitiesRich( pblock, blockType, nEntities, this );
      }
    }
    return nullptr;
  }

  auto  ContentsIndex::GetKeyStats( const std::string_view& key ) const -> BlockInfo
  {
    auto  pfound = contents.Search( { key.data(), key.size() } );

    if ( pfound != nullptr )
    {
      BlockInfo blockInfo;

      if ( ::FetchFrom( ::FetchFrom( pfound, blockInfo.bkType ), blockInfo.nCount ) != nullptr )
        return blockInfo;
    }
    return { uint32_t(-1), 0 };
  }

  auto  ContentsIndex::ListEntities( EntityId id ) -> mtc::api<IEntitiesList>
  {
    return new EntityIterator( entities.GetIterator( id ), this );
  }

  auto  ContentsIndex::ListEntities( uint32_t ix ) -> mtc::api<IEntitiesList>
  {
    return new EntityIterator( entities.GetIterator( ix ), this );
  }

  auto  ContentsIndex::ListContents( const std::string_view& key ) -> mtc::api<IContentsList>
  {
    return new LexemeIterator( this, key );
  }

  auto  ContentsIndex::Commit() -> mtc::api<IStorage::ISerialized>
  {
    return xStorage;
  }

  void  ContentsIndex::Remove()
  {
    return xStorage->Remove();
  }

  bool  ContentsIndex::delEntity( EntityId id, uint32_t index )
  {
    patchTab.Delete( { id.data(), id.size() }, index );
    shadowed.Set( index );
    return true;
  }

  // ContentsIndex::EntitiesBase implementation

  ContentsIndex::EntitiesBase::EntitiesBase(
    const mtc::api<const mtc::IByteBuffer>& src,
    uint32_t                                btp,
    uint32_t                                cnt,
    const ContentsIndex*                    own ):
      bkType( btp ),
      ncount( cnt ),
      limits{ 1, uint32_t(-1) },
      parent( own ),
      iblock( src ),
      origin( src->GetPtr() ),
      finish( origin + src->GetLen() ),
      ptrtop( origin )
  {
    if ( origin + 3 > finish )
      throw std::logic_error( "invalid block format @" __FILE__ ":" LINE_STRING );

    unsigned  idxlen =
      (unsigned(uint8_t(finish[-3])) << 0x10) |
      (unsigned(uint8_t(finish[-2])) << 0x08) |
      (unsigned(uint8_t(finish[-1])) << 0x00);

    if ( idxlen != 0 )
    {
      auto  src = finish - idxlen - 3;
      auto  end = finish - 3;
      auto  old = DocDowel{ 0, 0 };

      if ( origin + idxlen + 3 > finish )
        throw std::logic_error( "invalid block format @" __FILE__ ":" LINE_STRING );

      for ( pindex = new DocIndex(); src < end; )
      {
        uint32_t  addDoc;
        uint64_t  addPos;

        src = ::FetchFrom( ::FetchFrom( src, addDoc ), addPos );
          old.lastId += addDoc;
          old.offset += addPos;
        pindex->emplace_back( old );
      }
      dowBeg = pindex->data();
      dowEnd = pindex->data() + pindex->size();
    }
    finish -= (idxlen + 3);
  }

  ContentsIndex::EntitiesBase::EntitiesBase( const EntitiesBase& source, const Bounds& bounds ):
    bkType( source.bkType ),
    ncount( source.ncount ),
    limits( bounds ),
    parent( source.parent ),
    iblock( source.iblock ),
    origin( source.origin ),
    finish( source.finish ),
    ptrtop( source.ptrtop ),
    pindex( source.pindex ),
    dowBeg( source.dowBeg ),
    dowEnd( source.dowEnd )
  {
    if ( pindex != nullptr )
    {
      auto  lStart = DocDowel{ 0U, 0UL };
      auto  lLimit = dowBeg;

    // set lower limit to new navi position
      while ( dowBeg != dowEnd && dowBeg->lastId < bounds.uLower )
        lStart = *(lLimit = dowBeg++);

    // set upper limit to new navi position
      while ( dowEnd > dowBeg && dowEnd[-1].lastId > bounds.uUpper )
        --dowEnd;

    // check if new limits are empty
      if ( (dowBeg = lLimit) > dowEnd )
      {
        iblock = nullptr;
        ptrtop = finish;
      }
        else
      {
        curref.uEntity = lStart.lastId;
        ptrtop = origin + lStart.offset;
      }
    }
  }

  auto  ContentsIndex::EntitiesBase::Last() const -> uint32_t
  {
    return parent->GetMaxIndex();
  }

  // ContentsIndex::EntitiesLite implementation

  auto  ContentsIndex::EntitiesLite::Find( uint32_t tofind ) -> Reference
  {
    if ( (tofind = std::max( tofind, limits.uLower )) >= limits.uUpper )
      return curref = { uint32_t(-1), { nullptr, 0 } };

    if ( curref.uEntity >= tofind )
      return curref;

    while ( ptrtop < finish )
    {
      unsigned  udelta;

      if ( (ptrtop = ::FetchFrom( ptrtop, udelta )) == nullptr )
        break;

      if ( (curref.uEntity += udelta + 1) >= limits.uUpper )
        break;

      if ( curref.uEntity >= tofind && !parent->shadowed.Get( curref.uEntity ) )
        return curref;
    }
    return curref = { (uint32_t)-1, { nullptr, 0 } };
  }

  auto  ContentsIndex::EntitiesLite::Copy( const Bounds& bounds ) const -> mtc::api<IEntities>
  {
    auto  copied = mtc::api( new EntitiesLite( *this, bounds ) );

    return copied->iblock != nullptr ? copied.ptr() : nullptr;
  }

  // ContentsIndex::EntitiesRich implementation

  auto  ContentsIndex::EntitiesRich::Find( uint32_t tofind ) -> Reference
  {
    if ( (tofind = std::max( tofind, limits.uLower )) >= limits.uUpper )
      return curref = { uint32_t(-1), { nullptr, 0 } };

    if ( curref.uEntity >= tofind )
      return curref;

  // check in the documents index
    for ( ; dowBeg != dowEnd && dowBeg->lastId < tofind; ++dowBeg )
    {
      curref.uEntity = dowBeg->lastId;
        ptrtop = origin + dowBeg->offset;
    }

  // lookup in block
    while ( ptrtop < finish )
    {
      unsigned  udelta;
      unsigned  ublock;

      if ( (ptrtop = ::FetchFrom( ::FetchFrom( ptrtop, udelta ), ublock )) == nullptr )
        break;

      if ( (curref.uEntity += udelta + 1) >= limits.uUpper )
        break;

      if ( curref.uEntity >= tofind && !parent->shadowed.Get( curref.uEntity ) )
      {
        curref.details = { ptrtop, ublock };
        return ptrtop += ublock, curref;
      }
      ptrtop += ublock;
    }
    return curref = { (uint32_t)-1, { nullptr, 0 } };
  }

  auto  ContentsIndex::EntitiesRich::Copy( const Bounds& bounds ) const -> mtc::api<IEntities>
  {
    auto  copied = mtc::api( new EntitiesRich( *this, bounds ) );

    return copied->iblock != nullptr ? copied.ptr() : nullptr;
  }

  // ContentsIndex::EntityIterator implementation

  auto  ContentsIndex::EntityIterator::Curr() -> mtc::api<const IEntity>
  {
    for ( auto ent = iterator.Curr(); ent != nullptr; ent = iterator.Next() )
      if ( !contents->shadowed.Get( ent->GetIndex() ) )
      {
        auto  patched = contents->patchTab.Search( ent->GetIndex() );
        auto  bundled = Override::Entity( ent ).Bundle( contents->xStorage->Packages(), ent->GetPackPos() );

        return patched != nullptr ? Override::Entity( bundled ).Extra( patched ) : bundled;
      }

    return nullptr;
  }

  auto  ContentsIndex::EntityIterator::Next() -> mtc::api<const IEntity>
  {
    for ( auto ent = iterator.Next(); ent != nullptr; ent = iterator.Next() )
      if ( !contents->shadowed.Get( ent->GetIndex() ) )
      {
        auto  patched = contents->patchTab.Search( ent->GetIndex() );
        auto  bundled = Override::Entity( ent ).Bundle( contents->xStorage->Packages(), ent->GetPackPos() );

        return patched != nullptr ? Override::Entity( bundled ).Extra( patched ) : bundled;
      }

    return nullptr;
  }

  // ContentsIndex::PatchApplier implementation

  void  ContentsIndex::PatchApplier::Delete( EntityId entid )
  {
    auto  entptr = contents.entities.GetEntity( entid );

    if ( entptr != nullptr )
      contents.patchTab.Delete( entid, entptr->index );
  }

  void  ContentsIndex::PatchApplier::Update( EntityId entid, const void* mdata, size_t size )
  {
    auto  entptr = contents.entities.GetEntity( entid );

    if ( entptr != nullptr )
      contents.patchTab.Update( entid, entptr->index, { (const char*)mdata, size } );
  }

  // contents implementation

  auto  Index::Create( mtc::api<IStorage::ISerialized> serialized ) -> mtc::api<IContentsIndex>
  {
    if ( serialized->Entities() == nullptr )
      throw std::invalid_argument( "could not open index, no '.entities' found @" __FILE__ ":" LINE_STRING );
    return new ContentsIndex( serialized );
  }

  // ContentsIndex::LexemeIterator implementation

  ContentsIndex::LexemeIterator::LexemeIterator( ContentsIndex* pc, const std::string_view& pk ):
    contents( pc ),
    iterator( pc->contents.end() ),
    templStr( pk )
  {
    auto  ktop = templStr.data();
    auto  kend = templStr.data() + templStr.size();
    int   rcmp = 0;

    while ( ktop != kend && *ktop != '?' && *ktop != '*' )
      ++ktop;

    iterator = pc->contents.lower_bound( { templStr.data(), ktop } );

    if ( templStr.size() != 0 )
      while ( iterator != pc->contents.end() && (rcmp = strmatch( iterator->key, templStr )) < 0 )
        ++iterator;

    if ( rcmp > 0 )
      iterator = pc->contents.end();
  }

  auto  ContentsIndex::LexemeIterator::Curr() -> std::string
  {
    return iterator != contents->contents.end() ? iterator->key.to_string() : "";
  }

  auto  ContentsIndex::LexemeIterator::Next() -> std::string
  {
    if ( iterator != contents->contents.end() )
    {
      ++iterator;

      if ( templStr.size() != 0 )
      {
        int   rcmp = 0;

        while ( iterator != contents->contents.end() && (rcmp = strmatch( iterator->key, templStr )) < 0 )
          ++iterator;

        if ( rcmp > 0 )
          iterator = contents->contents.end();
      }
    }
    return iterator != contents->contents.end() ? iterator->key.to_string() : "";
  }

}}}
