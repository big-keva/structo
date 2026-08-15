# include "../../indexer/layered-contents.hpp"
# include "../../indexer/static-contents.hpp"
# include "../../indexer/dynamic-contents.hpp"
# include "../../exceptions.hpp"
# include "../../compat.hpp"
# include "override-entities.hpp"
# include "commit-contents.hpp"
# include "merger-contents.hpp"
# include "object-holders.hpp"
# include "index-layers.hpp"
# include <mtc/recursive_shared_mutex.hpp>
# include <shared_mutex>
# include <cmath>
#include <mtc/bitset.h>

namespace structo {
namespace indexer {
namespace layered {

  constexpr long  max_merge_threads = 2;

  class ContentsIndex final: public IContentsIndex
  {
    std::atomic_long  referenceCount = 0;

    long  Attach() override {  return ++referenceCount;  }
    long  Detach() override;

    class EntityIteratorByIx;
    class EntityIteratorById;

    struct IndexEntry
    {
      std::atomic<IndexEntry*>  pChain = nullptr;
      mtc::api<IContentsIndex>  pIndex;
      IndexEntry*               backup = nullptr;
      uint32_t                  uLower;
      uint32_t                  uUpper;
      uint32_t                  dwSets = 0;

    public:
      IndexEntry( uint32_t lo ): uLower( lo ) {}
      IndexEntry( uint32_t lo, mtc::api<IContentsIndex> ix, IndexEntry* pn );
      IndexEntry( uint32_t lo, uint32_t up, mtc::api<IContentsIndex> ix, IndexEntry* pn );
     ~IndexEntry();

    public:
      auto  Override( mtc::api<const IEntity> ) const -> mtc::api<const IEntity>;

    private:
      IndexEntry( const IndexEntry& ) = delete;
      IndexEntry& operator=( const IndexEntry& ) = delete;
    };

    using AtomicEntry = std::atomic<IndexEntry*>;

    struct MergeItems
    {
      AtomicEntry*  beg;
      AtomicEntry*  end;
      uint32_t      len;
    };

  public:
    ContentsIndex( const mtc::api<IContentsIndex>* indices, size_t count );
    ContentsIndex( const mtc::api<IStorage>&, const dynamic::Settings& );

    auto  StartMonitor( const std::chrono::seconds& mergeMonitorDelay ) -> ContentsIndex*;

  public:
    auto  GetEntity( EntityId ) const -> mtc::api<const IEntity> override;
    auto  GetEntity( uint32_t ) const -> mtc::api<const IEntity> override;

    bool  DelEntity( EntityId ) override;
    auto  SetEntity( EntityId, const mtc::span<const EntryView>&,
      const std::string_view&, const std::string_view& ) -> mtc::api<const IEntity> override;
    auto  SetExtras( EntityId, const std::string_view& ) -> mtc::api<const IEntity> override;

    void  StashEntity( EntityId ) override;

    auto  GetMaxIndex() const -> uint32_t override;
    auto  GetKeyBlock( const std::string_view& ) const -> mtc::api<IEntities> override;
    auto  GetKeyStats( const std::string_view& ) const -> BlockInfo override;

    auto  ListEntities( EntityId ) -> mtc::api<IEntitiesList> override;
    auto  ListEntities( uint32_t ) -> mtc::api<IEntitiesList> override;
    auto  ListContents( const std::string_view& ) -> mtc::api<IContentsList> override;

    auto  Commit() -> mtc::api<IStorage::ISerialized> override;
    auto  Reduce() -> mtc::api<IContentsIndex> override {  return this;  }
    void  Remove() override;

  protected:
    using EventRec = std::pair<void*, Notify::Event>;

    void  MergeMonitor( const std::chrono::seconds& );
    auto  SelectLimits() -> MergeItems;
    auto  WaitGetEvent( const std::chrono::seconds& ) -> EventRec;

  protected:
    mtc::api<IStorage>        istore;
    dynamic::Settings         dynSet;
    bool                      rdOnly = false;

    volatile bool             canRun = true;    // the continue flag

  // index manager - lock-free list
    AtomicEntry               layers = nullptr;
    mtc::api<IContentsIndex>  preDyn;           // предварительно зарезервированный динамический индекс

  // event manager - the events are processed after the index
  // asyncronous action is performed
    std::list<EventRec>       evQueue;
    std::mutex                evMutex;
    std::condition_variable   evEvent;
    std::thread               monitor;
    std::atomic_long          mergers = 0;
//    std::vector<uint64_t>     docHash;
//    size_t                    hashLen = 1024 * 1024;   // billion entities
  };

  class ContentsIndex::EntityIteratorByIx final: public IEntitiesList
  {
    struct IndexRefer
    {
      uint32_t                uLower;
      uint32_t                uUpper;
      mtc::api<IEntitiesList> pItems;
    };

  public:
    EntityIteratorByIx( ContentsIndex*, unsigned );

    auto  Curr() -> mtc::api<const IEntity> override;
    auto  Next() -> mtc::api<const IEntity> override;

  protected:
    std::list<IndexRefer>           refers;
    std::list<IndexRefer>::iterator refptr;

    implement_lifetime_control
  };

  class ContentsIndex::EntityIteratorById final: public IEntitiesList
  {
    struct IndexRefer
    {
      uint32_t                uLower;
      uint32_t                uUpper;
      mtc::api<IEntitiesList> pItems;
      mtc::api<const IEntity> entity;
      std::string_view        ent_id;
    };

  public:
    EntityIteratorById( ContentsIndex* parent, EntityId first );

    auto  Curr() -> mtc::api<const IEntity> override;
    auto  Next() -> mtc::api<const IEntity> override;

  protected:
    std::list<IndexRefer>           refers;

    implement_lifetime_control
  };

  // ContentsIndex::IndexEntry implementation

  ContentsIndex::IndexEntry::IndexEntry( uint32_t lower, mtc::api<IContentsIndex> index, IndexEntry* chain ):
    pChain( chain ),
    pIndex( index ),
    uLower( lower ),
    uUpper( uLower + index->GetMaxIndex() - 1 )
  {
  }

  ContentsIndex::IndexEntry::IndexEntry( uint32_t lower, uint32_t upper, mtc::api<IContentsIndex> index, IndexEntry* chain ):
    pChain( chain ),
    pIndex( index ),
    uLower( lower ),
    uUpper( upper )
  {
  }

  ContentsIndex::IndexEntry::~IndexEntry()
  {
  // remove backup elemenents
    while ( backup != nullptr )
    {
      auto  tofree = backup;
        backup = backup->pChain.load();
      delete tofree;
    }
  }

  auto  ContentsIndex::IndexEntry::Override( mtc::api<const IEntity> entity ) const -> mtc::api<const IEntity>
  {
    return uLower > 1 ? Override::Entity( entity ).Index(
      entity->GetIndex() + uLower - 1 ) : entity;
  }

  // ContentsIndex implementation

  ContentsIndex::ContentsIndex( const mtc::api<IContentsIndex>* indices, size_t count )
  {
    uint32_t  uLower = 1;

    for ( auto end = indices + count; indices != end; uLower += (*indices++)->GetMaxIndex() )
      layers.store( new IndexEntry( uLower, *indices, layers.load() ) );
  }

  ContentsIndex::ContentsIndex( const mtc::api<IStorage>& storage, const dynamic::Settings& dynSets ):
    istore( storage ),
    dynSet( dynSets )
  {
    auto  dwLower = uint32_t(1);

  // check if has any sources
    if ( auto sources = istore->ListIndices(); sources != nullptr )
      for ( auto serial = sources->Get(); serial != nullptr; serial = sources->Get() )
      {
        layers.store( new IndexEntry( dwLower, static_::Index().Create( serial ), layers.load() ) );
          dwLower += layers.load()->pIndex->GetMaxIndex();
      }

  // add dynamic index to the end if possible
    if ( auto dynamic = istore->CreateStore(); dynamic != nullptr )
    {
      layers.store( new IndexEntry( dwLower, dynamic::Index().Set( dynamic ).Set( dynSet ).Create(), layers.load() ) );
        layers.load()->uUpper = uint32_t(-1);
        layers.load()->dwSets = 1;

    // взять резервный динамический индекс для будущей ротации
      preDyn = dynamic::Index().Set( dynamic ).Set( dynSet ).Create();
      rdOnly = false;
    } else rdOnly = true;
  }

  auto  ContentsIndex::StartMonitor( const std::chrono::seconds& mergeMonitorDelay ) -> ContentsIndex*
  {
//    monitor = std::thread( &ContentsIndex::MergeMonitor, this, mergeMonitorDelay );
    return this;
  }

  long  ContentsIndex::Detach()
  {
    auto  rcount = --referenceCount;

    if ( rcount == 0 )
    {
      if ( monitor.joinable() )
      {
        canRun = false,
          evEvent.notify_one();
        monitor.join();
      }
//!!!      commitItems();
      delete this;
    }
    return rcount;
  }

  auto  ContentsIndex::GetEntity( EntityId id ) const -> mtc::api<const IEntity>
  {
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      if ( auto entry = layer->pIndex->GetEntity( id ); entry != nullptr )
        return layer->Override( entry );

    return {};
  }

  auto  ContentsIndex::GetEntity( uint32_t id ) const -> mtc::api<const IEntity>
  {
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      if ( layer->uLower <= id && layer->uUpper >= id )
        if ( auto entry = layer->pIndex->GetEntity( id - layer->uLower + 1 ); entry != nullptr )
          return layer->Override( entry );

    return {};
  }

  bool  ContentsIndex::DelEntity( EntityId id )
  {
    bool  deleted = false;

    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      deleted |= layer->pIndex->DelEntity( id );

    return deleted;
  }

  auto  ContentsIndex::SetEntity( EntityId id,
    const mtc::span<const EntryView>& keys,
    const std::string_view&           xtra,
    const std::string_view&           beef ) -> mtc::api<const IEntity>
  {
    auto  player = layers.load();

    if ( player == nullptr )
      throw std::logic_error( "index flakes are not initialized" );

    for ( ; ; )
    {
    // try SetEntity(...) to the last index in the chain;
    // ** mark the index as containing actual id verion      auto  pindex = player->pIndex.ptr();       // the last index pointer, unchanged in one thread
      try
      {
        if ( auto thedoc = player->pIndex->SetEntity( id, keys, xtra, beef ); thedoc != nullptr )
        {
/*          auto  dwhash = std::hash<std::string_view>()( id );
          auto  hindex = dwhash & (hashLen - 1);

          if ( mtc::bitset_get( docHash, hindex ) )
          {
            for ( auto beg = player->pChain.load(); beg != nullptr; beg = beg->pChain.load() )
              beg->pIndex->DelEntity( id );
          }
            else
          mtc::bitset_set( docHash, hindex );
*/
          return player->Override( thedoc );
        }
      }

    // on dynamic index overflow, rotate the last index
      catch ( const index_overflow& /*xo*/ )
      {
        static std::mutex mutex;
        auto lock = mtc::make_unique_lock( mutex );
      // create new entry for dynamic index and mark as non-mergeable
        auto  newptr = std::make_unique<IndexEntry>( player->uUpper + 1, uint32_t(-1), preDyn, player );
          newptr->dwSets = 1;

      // try rotate index; if already rotated by another thread, continue attempts
      // to insert entity
        if ( !layers.compare_exchange_strong( player, newptr.get() ) )
          continue;

      // if replaced, reallocate the new dynamic index
        preDyn = dynamic::Index()
          .Set( dynSet )
          .Set( istore->CreateStore() ).Create();

      // change index to commiter
        player->uUpper = player->uLower + player->pIndex->GetMaxIndex() - 1;
        player->pIndex = commit::Contents().Create( player->pIndex, [this]( void* to, Notify::Event event )
          {
            mtc::interlocked( mtc::make_unique_lock( evMutex ), [&]()
              {  evQueue.emplace_back( to, event );  } );
            evEvent.notify_one();
          } );
        player = newptr.release();
      }
    }
  }

  auto  ContentsIndex::SetExtras( EntityId id, const std::string_view& xtras ) -> mtc::api<const IEntity>
  {
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      if ( auto entity = layer->pIndex->SetExtras( id, xtras ); entity != nullptr )
        return layer->Override( entity );

    return {};
  }

  auto ContentsIndex::ListEntities( EntityId start ) -> mtc::api<IEntitiesList>
  {
    return new EntityIteratorById( this, start );
  }

  auto ContentsIndex::ListEntities( uint32_t start ) -> mtc::api<IEntitiesList>
  {
    return new EntityIteratorByIx( this, start );
  }

  auto  ContentsIndex::Commit() -> mtc::api<IStorage::ISerialized>
  {
    for ( auto player = layers.load(); player != nullptr; player = player->pChain.load() )
    {
// !!!
    }
    return nullptr;
  }

  void  ContentsIndex::Remove()
  {
    throw std::runtime_error( "not implemented @" __FILE__ ":" LINE_STRING );
  }

  void  ContentsIndex::StashEntity( EntityId )
  {
    throw std::logic_error( "must not be called @" __FILE__ ":" LINE_STRING );
  }

  auto  ContentsIndex::GetMaxIndex() const -> uint32_t
  {
    if ( auto player = layers.load(); player != nullptr )
      return player->uLower + player->pIndex->GetMaxIndex() - 1;

    return 0;
  }

  auto  ContentsIndex::GetKeyBlock( const std::string_view& key ) const -> mtc::api<IEntities>
  {
    auto  entities = mtc::api( new EntitiesChain( this ) );

    // fill blocks to the entities holder
    for ( auto layer = layers.load(); layer != nullptr; layer = layer->pChain.load() )
      if ( auto block = layer->pIndex->GetKeyBlock( key ); block != nullptr )
        entities->AddBlock( { layer->uLower, layer->uUpper, block }, EntitiesChain::to_head );

    // check if blocks layers has only one block
    return entities->Size() != 0 ? entities.ptr() : nullptr;
  }

  auto  ContentsIndex::GetKeyStats( const std::string_view& key ) const -> BlockInfo
  {
    auto  blockStats = BlockInfo{ uint32_t(-1), 0 };

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

  auto  ContentsIndex::ListContents( const std::string_view& key ) -> mtc::api<IContentsList>
  {
    /*!!!
    return listContents( key, MakeObjectHolder( mtc::api( (const Iface*)this ),
      std::move( mtc::make_shared_lock( ixlock ) ) ) );
     */
  }

  void  ContentsIndex::MergeMonitor( const std::chrono::seconds& startDelay )
  {
    pthread_setname_np( pthread_self(), "MergeMonitor" );

    for ( std::this_thread::sleep_for( startDelay ); canRun; )
    {
      auto  evNext = WaitGetEvent( std::chrono::seconds( 30 ) );

    // for event occured, search the element in the list of indices to Reduce()
    // and finish index modification
      if ( evNext.first != nullptr && canRun )
      {
        auto  ppswap = &layers;

      // search index to be swapped
        while ( ppswap->load() != nullptr && ppswap->load()->pIndex.ptr() != evNext.first )
          ppswap = &ppswap->load()->pChain;

      // if the index with key pointer found, check the type of event occured
        if ( ppswap->load() == nullptr )
          throw std::logic_error( "strange event not attached to any index!" );

        switch ( evNext.second )
        {
        // On OK, replace the index in the entry to it's reduced version,
        // resort the indices in the size-decreasing order, and renumber
          case Notify::Event::OK:
            {
              auto  player = ppswap->load();
            /*
              auto  newone = std::make_unique<IndexEntry>(
                player->uLower,
                player->uUpper,
                player->pIndex->Reduce(),
                player->pChain.load() );

              if ( !ppswap->compare_exchange_strong( player, newone.release() ) )
                throw std::logic_error( "index chain was modified outsize of MergeMonitor @" __FILE__ ":" LINE_STRING );

              delete player;
             */
            }
            break;

        // On Empty, simple remove the existing index because its processing
        // result is empty
          case Notify::Event::Empty:
            {
              auto  player = ppswap->load();

              if ( !ppswap->compare_exchange_strong( player, player->pChain.load() ) )
                throw std::logic_error( "index chain was modified outsize of MergeMonitor @" __FILE__ ":" LINE_STRING );

              delete player;
            }
            break;

        // On Cancel, rollback the event record to the previous subset
        // of entries saved in the entry processed
          case Notify::Event::Canceled:
            {
              fprintf( stderr, "Cancelled\n" );

              auto  toswap = ppswap->load();
              auto  backup = toswap->backup;
              auto  pplink = &backup->pChain;

            // check if valid
              if ( backup == nullptr )
                throw std::logic_error( "strange event with nullptr backup value @" __FILE__ ":" LINE_STRING "!" );

            // link backup
              while ( pplink->load() != nullptr )
                pplink = &pplink->load()->pChain;
              pplink->store( toswap->pChain.load( std::memory_order_acquire ), std::memory_order_relaxed );

            // remove swapped index
              ppswap->store( backup, std::memory_order_release );
              delete toswap;
            }
            break;

      // On Failed, commit index and shutdown service if possible
          default:
            break;
        }
      }

    // try select indices to be merged
      if ( false && canRun )
      {
        auto  limits = SelectLimits();      // select area to be merged

      // select the limits, check and select again the limits for merger
        if ( limits.beg != limits.end )
        {
          fprintf( stderr, "selected %d indices:\n", limits.len );

          for ( auto p = limits.beg; p != nullptr; )
          {
            fprintf( stderr, "\t%lx\n", p->load() );
            if ( p == limits.end )  break;
              else p = &p->load()->pChain;
          }

          auto  pentry = std::make_unique<IndexEntry>(
            limits.beg->load()->uLower );
          auto  xMaker = fusion::Contents()
            .Set( [this]( void* to, Notify::Event event )
              {
                mtc::interlocked( mtc::make_unique_lock( evMutex ), [&]()
                  {  evQueue.emplace_back( to, event );  } );
                --mergers;
                  evEvent.notify_one();
              } )
//              .Set( canContinue )
            .Set( istore->CreateStore() );

        // fill merger list
          for ( auto next = limits.beg; ; )
          {
            auto loaded = next->load();

            xMaker.Add( next->load()->pIndex,
              fusion::Contents::to_head );
            if ( next != limits.end ) next = &next->load()->pChain;
              else break;
          }

          pentry->pChain = limits.beg->load()->pChain.load();
          pentry->pIndex = xMaker.Create();
          pentry->backup = limits.beg->load();
          pentry->uUpper = limits.beg->load()->uUpper;
          pentry->dwSets = 1;

        // link index to che chain and unlink backup from the list
          limits.beg->store( pentry.release() );
          limits.end->load()->pChain.store( nullptr );

          ++mergers;
        }
      }
    }
  }

 /*
  * Ищет самую длинную постедовательность самых маленьких индексов. Критерий -
  */

  auto  ContentsIndex::SelectLimits() -> MergeItems
  {
    struct LayerStats
    {
      AtomicEntry*  pLayer;
      uint32_t      uCount;
    };
    auto  asizes = std::vector<LayerStats>();
    auto  select = MergeItems();
    float srange;
    auto  Ranker = [&]( size_t from, size_t to ) -> float
    {
      auto  length = double(to - from + 1);
      auto  weight = double(0);
      auto  sqSumm = double(0);

      // Считаем сумму весов и сумму квадратов весов только внутри интервала
      for ( size_t i = from; i <= to; ++i )
      {
        weight += asizes[i].uCount;
        sqSumm += asizes[i].uCount * asizes[i].uCount;
      }
      weight /= length;     // Средний вес элемента на этом отрезке

      // Расчет дисперсии и СКО (квадратичного отклонения весов элементов от их среднего)
      const double mean_of_squares = sqSumm / length;
      const double variance = mean_of_squares - (weight * weight);
      const double weight_sigma = (variance > 0.0) ? std::sqrt(variance) : 0.0;

      // Формула оценки (Score):
      // Длина выступает как положительный множитель.
      // СКО весов выступает как штраф (чем стабильнее вес элементов, тем меньше штраф).
      // Добавляем 1.0 к sigma, чтобы избежать деления на ноль при идеальном совпадении весов.
      return length / (1.0 + weight_sigma);
    };

    if ( mergers.load() >= max_merge_threads )
      return select;

    for ( auto next = &layers; next->load() != nullptr; next = &next->load()->pChain )
    {
      auto  entry = next->load();

      if ( entry->dwSets == 0 )
        asizes.push_back( { next, next->load()->pIndex->GetMaxIndex() } );
      else
        asizes.push_back( { nullptr, 0 } );
    }

    for ( size_t from = 0; from != asizes.size(); ++from )
      if ( asizes[from].pLayer != nullptr )
        for ( size_t to = from + 1; to < asizes.size() && asizes[to].pLayer != nullptr; ++to )
        {
          float crange = Ranker( from, to );

          if ( select.beg == select.end || crange > srange )
          {
            select = { asizes[from].pLayer, asizes[to].pLayer, uint32_t(to - from + 1) };
            srange = crange;
          }
        }
    return select;
  }

// check if any events occured; process events first
// for each processed event, either reduce the index sent the event,
// or simply remove the empty index
// or process the errors
  auto  ContentsIndex::WaitGetEvent( const std::chrono::seconds& timeout ) -> EventRec
  {
    auto  exwait = mtc::make_unique_lock( evMutex );
    auto  fEvent = [this](){  return !canRun || !evQueue.empty();  };

    if ( evEvent.wait_for( exwait, timeout, fEvent ) && canRun )
    {
      auto  ev_get = evQueue.front();
        evQueue.pop_front();
      return ev_get;
    }
    return { nullptr, Notify::Event::None };
  }

  // ContentsIndex::EntityIteratorByIx impl

  ContentsIndex::EntityIteratorByIx::EntityIteratorByIx( ContentsIndex* parent, unsigned first )
  {
   /*
     !!!
    auto  itnext = mtc::api<IEntitiesList>{};

    for ( auto& next: parent->layers )
      if ( (itnext = next.pIndex->ListEntities( 0U )) != nullptr )
        refers.push_back( { next.uLower, next.uUpper, itnext } );

    refptr = refers.begin();
    */
  }

  auto  ContentsIndex::EntityIteratorByIx::Curr() -> mtc::api<const IEntity>
  {
    auto  getdoc = mtc::api<const IEntity>{};

    while ( refptr != refers.end() )
    {
      if ( (getdoc = refptr->pItems->Curr()) != nullptr )
        return Override::Entity( getdoc ).Index( getdoc->GetIndex() + refptr->uLower - 1 );

      refptr = refers.erase( refptr );
    }
    return nullptr;
  }

  auto  ContentsIndex::EntityIteratorByIx::Next() -> mtc::api<const IEntity>
  {
    auto  fnLoad = &IEntitiesList::Next;
    auto  getdoc = mtc::api<const IEntity>{};

    while ( refptr != refers.end() )
    {
      if ( (getdoc = (refptr->pItems.ptr()->*fnLoad)()) != nullptr )
        return Override::Entity( getdoc ).Index( getdoc->GetIndex() + refptr->uLower - 1 );

      refptr = refers.erase( refptr );
      fnLoad = &IEntitiesList::Curr;
    }
    return nullptr;
  }

  // ContentsIndex::EntityIteratorById impl

  ContentsIndex::EntityIteratorById::EntityIteratorById( ContentsIndex* parent, EntityId first )
  {
    /*
     !!!
    auto  itnext = mtc::api<IEntitiesList>();
    auto  getdoc = mtc::api<const IEntity>();

    for ( auto& next: parent->layers )
      if ( (itnext = next.pIndex->ListEntities( first )) != nullptr && (getdoc = itnext->Curr()) != nullptr )
        refers.push_back( { next.uLower, next.uUpper, itnext, getdoc, getdoc->GetId() } );
    */
  }

  auto  ContentsIndex::EntityIteratorById::Curr() -> mtc::api<const IEntity>
  {
    auto  pfirst = refers.begin();
    auto  minone = pfirst;

  // select smallest document
    if ( minone != refers.end() )
    {
      while ( ++pfirst != refers.end() )
        if ( pfirst->ent_id < minone->ent_id )
          minone = pfirst;

      return Override::Entity( minone->entity ).Index( minone->entity->GetIndex() + minone->uLower - 1 );
    }
    return nullptr;
  }

  auto  ContentsIndex::EntityIteratorById::Next() -> mtc::api<const IEntity>
  {
    auto  pfirst = refers.begin();
    auto  minone = pfirst;

    if ( pfirst != refers.end() )
    {
    // search minimal one
      while ( ++pfirst != refers.end() )
        if ( pfirst->ent_id < minone->ent_id )
          minone = pfirst;

    // move minimal element to next
      if ( (minone->entity = minone->pItems->Next()) != nullptr ) minone->ent_id = minone->entity->GetId();
        else refers.erase( minone );

      if ( (minone = pfirst = refers.begin()) != refers.end() )
      {
      // search minmal again
        while ( ++pfirst != refers.end() )
          if ( pfirst->ent_id < minone->ent_id )
            minone = pfirst;

        return Override::Entity( minone->entity ).Index( minone->entity->GetIndex() + minone->uLower - 1 );
      }
    }
    return nullptr;
  }

  // Index implementation

  Index::Index( mtc::api<IStorage> ps ): contentsStorage( ps )
  {
  }

  auto  Index::Set( mtc::api<IStorage> ps ) -> Index&
  {
    return contentsStorage = ps, *this;
  }

  auto Index::Set( const dynamic::Settings& settings ) -> Index&
  {
    return dynamicSettings = settings, *this;
  }

  auto Index::Create() -> mtc::api<IContentsIndex>
  {
    if ( contentsStorage == nullptr )
      throw std::logic_error( "layered index storage is not defined" );
    return (new ContentsIndex( contentsStorage, dynamicSettings ))->StartMonitor( runMonitorDelay );
  }

  auto  Index::Create( const mtc::api<IContentsIndex>* indices, size_t size ) -> mtc::api<IContentsIndex>
  {
    return new ContentsIndex( indices, size );
  }

  auto  Index::Create( const std::vector<mtc::api<IContentsIndex>>& indices ) -> mtc::api<IContentsIndex>
  {
    return Create( indices.data(), indices.size() );
  }

}}}
