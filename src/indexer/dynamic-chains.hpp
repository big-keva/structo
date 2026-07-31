# if !defined( __structo_src_indexer_dynamic_chains_hxx__ )
# define __structo_src_indexer_dynamic_chains_hxx__
# include "../../contents.hpp"
# include "../../compat.hpp"
# include "dynamic-chains-ringbuffer.hpp"
# include "dynamic-bitmap.hpp"
# include "strmatch.hpp"
# include <mtc/recursive_shared_mutex.hpp>
# include <mtc/radix-tree.hpp>
# include <condition_variable>
# include <thread>
# include <atomic>

namespace structo {
namespace indexer {
namespace dynamic {

  enum: size_t
  {
    ring_buffer_size = 0x1000
  };

  template <class Allocator = std::allocator<char>>
  class BlockChains
  {
    struct ChainLink;
    struct ChainHook;

    using AtomicLink = std::atomic<ChainLink*>;
    using AtomicHook = std::atomic<ChainHook*>;

    using LinkAllocator = AllocatorCast<Allocator, ChainLink>;
    using HookAllocator = AllocatorCast<Allocator, AtomicHook>;

    enum: size_t
    {
      hash_table_size = 65521
    };

  /*
   * ChainLink represents one entity identified by virtual index iEnt data block
   * linked in a chain of blocks in increment order of entity indices
   */
    struct ChainLink
    {
      AtomicLink  p_next = nullptr;
      uint32_t    entity;
      uint32_t    lblock;

    public:
      template <class T>
      ChainLink( uint32_t ent, const T& blk ): entity( ent )
        {  memcpy( data(), blk.data(), lblock = uint32_t(blk.size()) );  }

    public:
      auto  data() const -> const char*  {  return (const char*)(this + 1);  }
      auto  data() ->             char*  {  return (      char*)(this + 1);  }

    };

  /*
   * ChainHook holds key body and reference to the first element in the chain
   * of blocks indexed by incremental virtual entity indices
   */
    struct ChainHook
    {
      using LastAnchor = std::atomic<AtomicLink**>;

      const size_t          nhCode;
      const unsigned        bkType;
      const size_t          cchkey;               // key length

      LinkAllocator         malloc;

      ChainHook*            pchain;               // collisions

      AtomicLink            pfirst = nullptr;     // first in chain

      AtomicLink*           points[32];           // points cache

      alignas(64)
      std::atomic_uint32_t  pindex = 0;
      alignas(64)
      std::atomic_uint32_t  ncount = 0;

    public:
      auto  data() const -> const char* {  return (const char*)(this + 1);  }
      auto  data() -> char* {  return (char*)(this + 1);  }

    public:
      ChainHook( const std::string_view& key, size_t hashCode, unsigned blockType, ChainHook*, Allocator );
     ~ChainHook();

    public:
      bool  operator == ( const std::string_view& s ) const
        {  return cchkey == s.size() && memcmp( data(), s.data(), s.size() ) == 0;  }
      bool  operator != ( const std::string_view& s ) const
        {  return !(*this == s);  }

    public:
      void  Insert( uint32_t entity, const std::string_view& block );
     /*
      * bool  Verify() const;
      *
      * for tesing:
      *   verifies the chain of entities to increment values
      *   element to element
      */
      bool  Verify() const;
      template <class OtherAllocator>
      auto  Remove( const Bitmap<OtherAllocator>& ) -> ChainHook&;

    };

  public:
    class KeyLister;

    BlockChains( Allocator alloc = Allocator() );
   ~BlockChains();

    void  Insert( const std::string_view& key, uint32_t entity, const std::string_view& block, unsigned bkType );
    auto  Lookup( const std::string_view& key ) const -> const ChainHook*;
    template <class OtherAllocator>
    auto  Remove( const Bitmap<OtherAllocator>& ) -> BlockChains&;
    auto  StopIt() -> BlockChains&;

    auto  ListKeys( const std::string_view& ) const -> KeyLister;
    auto  KeyCount() const -> size_t;

   /*
    * bool  Verify() const;
    *
    * for tesing: lists all the keys and checks integrity
    */
    bool  Verify() const;

  public:     //serialization
    template <class O1, class O2>
    auto  Serialize( O1*, O2* ) -> uint64_t;
    bool  VerifyIds( unsigned ) const;

  protected:
    void  KeysIndexer();

  protected:
    struct RadixLink
    {
      ChainHook*  blocksChain;
      uint64_t    blockOffset;
      uint32_t    blockLength;

      size_t  GetBufLen() const
      {
        return ::GetBufLen( blocksChain->bkType ) + ::GetBufLen( blocksChain->ncount.load() )
           + ::GetBufLen( blockOffset )
           + ::GetBufLen( blockLength ) + ::GetBufLen( 0 );
      }
      template <class O>
      O*    Serialize( O* o ) const
      {
        return
          ::Serialize( ::Serialize( ::Serialize( ::Serialize( ::Serialize( o, blocksChain->bkType ), blocksChain->ncount.load() ),
            blockOffset ),
            blockLength ), 0 );
      }
    };

    std::vector<AtomicHook, HookAllocator>    hashTable;
    AllocatorCast<Allocator, ChainHook>       hookAlloc;

    mtc::radix::tree<RadixLink,
      AllocatorCast<Allocator, RadixLink>>    radixTree;    // parallel radix tree
    mutable std::shared_mutex                 radixLock;    // locker to access

    RingBuffer<ChainHook*, ring_buffer_size>  keysQueue;    // queue for keys indexing
    std::condition_variable_any               keySyncro;    // syncro for shadow indexing keys
    std::thread                               keyThread;    // shadow keys indexer
    volatile bool                             runThread = false;

  };

  template <class Allocator>
  class BlockChains<Allocator>::KeyLister
  {
    using const_it_type = typename mtc::radix::tree<RadixLink, AllocatorCast<Allocator, RadixLink>>::
      template const_iterator<std::allocator<char>>;

  public:
    KeyLister( std::shared_mutex&, const_it_type&&, const_it_type&&, std::string&& );
    KeyLister( std::shared_mutex&, const_it_type&&, const_it_type&& );

    auto  CurrentKey() -> std::string;
    auto  GetNextKey() -> std::string;

  protected:
    std::shared_lock<std::shared_mutex> lck;
    const_it_type                       beg;
    const_it_type                       end;
    std::string                         tpl;

  };

// KeyBlockChains template implementation
// KeyBlockChains template implementation

  template <class Allocator>
  BlockChains<Allocator>::BlockChains( Allocator alloc ):
    hashTable( hash_table_size, alloc ),
    hookAlloc( alloc ),
    radixTree( alloc )
  {
    for ( keyThread = std::thread( &BlockChains<Allocator>::KeysIndexer, this ); !runThread; )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }

  template <class Allocator>
  BlockChains<Allocator>::~BlockChains()
  {
    StopIt();

    for ( auto& next: hashTable )
      for ( auto tostep = next.load(), tofree = tostep; tofree != nullptr; tofree = tostep )
      {
        tostep = tofree->pchain;
          tofree->~ChainHook();
        hookAlloc.deallocate( tofree, 0 );
      }
  }

  template <class Allocator>
  void  BlockChains<Allocator>::Insert( const std::string_view& key, uint32_t entity, const std::string_view& block, unsigned bkType )
  {
    auto  nhcode = std::hash<std::string_view>()( key );
    auto  hindex = nhcode % hashTable.size();
    auto& hentry = hashTable[hindex];
    auto  hvalue = mtc::ptr::clean( hentry.load( std::memory_order_acquire ) );

    // check block type; set the type value if is not set yet
    if ( bkType == unsigned(-1) )
      bkType = block.size() != 0 ? 0x10 : 0;

    // lookup the collision chain for the element with searched key
    for ( ; hvalue != nullptr; hvalue = hvalue->pchain )
      if ( hvalue->nhCode == nhcode && *hvalue == key )
      {
        if ( hvalue->bkType != bkType )
          throw std::invalid_argument( "Block type do not match the previously defined type" );
        return hvalue->Insert( entity, block );
      }

    // OK, try lock current entry with 'dirty' bit
    while ( !hentry.compare_exchange_strong( hvalue, mtc::ptr::dirty( hvalue ),
      std::memory_order_acq_rel,
      std::memory_order_acquire ) ) hvalue = mtc::ptr::clean( hvalue );

    // lookup the list got again searching for existing key
    for ( ; hvalue != nullptr; hvalue = hvalue->pchain )
      if ( hvalue->nhCode == nhcode && *hvalue == key )
      {
        hentry.store( mtc::ptr::clean( hentry.load(
          std::memory_order_acquire ) ),
          std::memory_order_release );

        if ( hvalue->bkType != bkType )
          throw std::invalid_argument( "Block type do not match the previously defined type" );

        return hvalue->Insert( entity, block );
      }

    // list contains no needed entry; allocate new ChainHook for new key;
    // for possible exceptions, unlock the entry and continue tracing
    try
    {
      new( hvalue = hookAlloc.allocate( (sizeof(ChainHook) * 2 + key.size() - 1) / sizeof(ChainHook) ) )
        ChainHook( key, nhcode, bkType, mtc::ptr::clean( hentry.load() ), hookAlloc );

      hentry.store( hvalue, std::memory_order_release );

      keysQueue.Put( hvalue );
      keySyncro.notify_one();
    }
    catch ( ... )
    {
      hentry.store( mtc::ptr::clean( hentry.load(
        std::memory_order_acquire) ),
        std::memory_order_release );
      throw;
    }
    return hvalue->Insert( entity, block );
  }

  template <class Allocator>
  auto  BlockChains<Allocator>::Lookup( const std::string_view& key ) const -> const ChainHook*
  {
    auto  nhcode = std::hash<std::string_view>()( key );
    auto  hindex = nhcode % hashTable.size();
    auto& hentry = hashTable[hindex];
    auto  hvalue = mtc::ptr::clean( hentry.load( std::memory_order_acquire ) );

  // first try find existing block in the hash chain
    for ( ; hvalue != nullptr; hvalue = hvalue->pchain )
      if ( hvalue->nhCode == nhcode && *hvalue == key )
        return hvalue;

    return nullptr;
  }

  template <class Allocator>
  auto  BlockChains<Allocator>::StopIt() -> BlockChains&
  {
    if ( keyThread.joinable() )
    {
      while ( !runThread )
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

      runThread = false;
      keySyncro.notify_one();
      keyThread.join();
    }
    return *this;
  }

  template <class Allocator>
  auto  BlockChains<Allocator>::ListKeys( const std::string_view& key ) const -> KeyLister
  {
    auto  templStr = std::string( key.begin(), key.end() );
    auto  templLen = size_t(0);
    auto  treeLock = mtc::make_shared_lock( radixLock, std::defer_lock );
    auto  radixBeg = radixTree.cend( std::allocator<char>() );
    auto  radixEnd = radixTree.cend( std::allocator<char>() );

    for ( ; templLen < templStr.size() && templStr[templLen] != '*' && templStr[templLen] != '?'; ++templLen )
      (void)NULL;

    treeLock.lock();

    if ( templLen != 0 )  radixBeg = radixTree.lower_bound( { templStr.data(), templLen }, std::allocator<char>() );
      else radixBeg = radixTree.cbegin( std::allocator<char>() );

    return KeyLister( radixLock, std::move( radixBeg ), std::move( radixEnd ), std::move( templStr ) );
  }

  template <class Allocator>
  auto  BlockChains<Allocator>::KeyCount() const -> size_t
  {
    auto  treeLock = mtc::make_shared_lock( radixLock, std::defer_lock );

    return radixTree.size();
  }

  template <class Allocator>
  template <class OtherAllocator>
  auto  BlockChains<Allocator>::Remove( const Bitmap<OtherAllocator>& deleted ) -> BlockChains&
  {
    for ( auto it = radixTree.begin(); it != radixTree.end(); )
    {
      if ( it->second.blocksChain->Remove( deleted ).ncount == 0 )
        it = radixTree.erase( it );
      else ++it;
    }
    return *this;
  }

  template <class Allocator>
  bool  BlockChains<Allocator>::Verify() const
  {
    for ( auto& next: hashTable )
      for ( auto verify = next.load(); verify != nullptr; verify = verify->pchain )
        if ( !verify->Verify() )
          return false;
    return true;
  }

 /*
  * Serialize( index, chain )
  *
  * Serializes the created inverted index to storage.
  */
  template <class Allocator>
  template <class O1, class O2>
  auto  BlockChains<Allocator>::Serialize( O1* index, O2* chain ) -> uint64_t
  {
    uint64_t  offset = 0;

# if defined( VERIFY_KEY_COUNT )
    // для уверенности в том, что KeysIndexer ничего не промотал, проверить совпадение количества
    // ключей в hash-table и в radixTree
    for ( auto& next: hashTable )
      for ( auto tostep = next.load(); tostep != nullptr; tostep = tostep->pchain.load() )
        if ( radixTree.Search( { tostep->data(), tostep->cchkey } ) == nullptr )
        {
          fprintf( stderr, "key '%s' not found in radix tree\n",
            std::string( tostep->data(), tostep->cchkey ).c_str() );
        }
# endif   // VERIFY_KEY_COUNT

  // store all the index chains saving offset, count and length to the tree
    for ( auto next = radixTree.begin(), stop = radixTree.end(); next != stop && chain != nullptr; ++next )
    {
      auto    lastId = uint32_t(0);
      auto    length = uint32_t(0);
      char    docbuf[0x20];
      size_t  doclen;

      next->value.blockOffset = offset;

    // store block according to block type:
    //  * blocks without coordinates;
    //  * blocks with coordinates
      if ( next->value.blocksChain->bkType == 0 )
      {
        for ( auto p = next->value.blocksChain->pfirst.load(); p != nullptr; p = p->p_next.load() )
          if ( p->entity != uint32_t(-1) )
          {
            doclen = ::Serialize( docbuf, p->entity - lastId - 1 ) - docbuf;
              length += doclen;
            chain = ::Serialize( chain, docbuf, doclen );
              lastId = p->entity;
          }
      }
        else
      {
        for ( auto p = next->value.blocksChain->pfirst.load(); p != nullptr; p = p->p_next.load() )
          if ( p->entity != uint32_t(-1) )
          {
            doclen = ::Serialize( ::Serialize( docbuf, p->entity - lastId - 1 ), p->lblock ) - docbuf;
              length += doclen + p->lblock;
            chain = ::Serialize( ::Serialize( chain,
              docbuf, doclen ), p->data(), p->lblock );
            lastId = p->entity;
          }
      }
      offset += (next->value.blockLength = length);
    }

  // store radix tree
    if ( chain == nullptr )
      return uint64_t(-1);

    if ( (index = radixTree.Serialize( index )) == nullptr )
      return uint64_t(-1);

    return offset;
  }

  template <class Allocator>
  bool  BlockChains<Allocator>::VerifyIds( unsigned maxIndex ) const
  {
    // store all the index chains saving offset, count and length to the tree
    for ( auto next = radixTree.begin(), stop = radixTree.end(); next != stop; ++next )
    {
      for ( auto p = next->value.blocksChain->pfirst.load(); p != nullptr; p = p->p_next.load() )
        if ( p->entity != uint32_t(-1) && p->entity > maxIndex )
          return false;
    }
    return true;
  }

 /*
  * Shadow keys indexer
  *
  * Wakes up on signals, indexes all the new keys from the queue, stops after
  * all the keys are indexed and runThread is false.
  */
  template <class Allocator>
  void  BlockChains<Allocator>::KeysIndexer()
  {
    auto        locker = mtc::make_unique_lock( radixLock );
    ChainHook*  addkey;

    pthread_setname_np( pthread_self(), "KeysIndexer" );

    for ( runThread = true; runThread; )
    {
      keySyncro.wait_for( locker,
        std::chrono::milliseconds( 100 ) );
      while ( keysQueue.Get( addkey ) )
        radixTree.Insert( { addkey->data(), addkey->cchkey }, { addkey, 0, 0 } );
    }
  }

  template <class Allocator>
  BlockChains<Allocator>::ChainHook::ChainHook( const std::string_view& key, size_t hashCode, unsigned b, ChainHook* p, Allocator m ):
    nhCode( hashCode ),
    bkType( b ),
    cchkey( key.size() ),
    malloc( m ),
    pchain( p )
  {
    memset( points, 0, sizeof(points) );
    memcpy( data(), key.data(), cchkey );
  }

  template <class Allocator>
  BlockChains<Allocator>::ChainHook::~ChainHook()
  {
    for ( auto pnext = pfirst.load(), pfree = pnext; pfree != nullptr; pfree = pnext )
    {
      pnext = pfree->p_next.load();
        pfree->~ChainLink();
      malloc.deallocate( pfree, 0 );
    }
  }

  template <class Allocator>
  void  BlockChains<Allocator>::ChainHook::Insert( uint32_t entity, const std::string_view& block )
  {
    auto          newptr = new( malloc.allocate( (sizeof(ChainLink) * 2 + block.size() - 1) / sizeof(ChainLink) ) )
      ChainLink( entity, block );
    AtomicLink*   pstore = &pfirst;
    ChainLink*    pentry;

  // check if no elements available and try write first element;
  // if succeeded, increment element count and return
    if ( pfirst.compare_exchange_strong( pentry = nullptr, newptr ) )
      return void(ncount.fetch_add( 1, std::memory_order_relaxed ));

  // если в цепочке мало элементов, перебираем от начала списка; если становится больше 16,
  // начинаем строить массив "последних добавленных" и смотреть по нему
    if ( ncount.load( std::memory_order_relaxed ) >= 16 )
    {
      auto  stopat = pindex.load( std::memory_order_relaxed );

      for ( auto  uindex = stopat - 1; (uindex % 32) != (stopat % 32) && points[uindex % 32] != nullptr; --uindex )
        if ( points[uindex % 32]->load( std::memory_order_acquire )->entity < entity )
          {  (pstore = points[uindex % 32])->load();  break;  }
    }

  // теперь отмотать вправо до первого элемента, чей идентификатор будет больше вставляемого
    for ( pentry = pstore->load(); pentry != nullptr && pentry->entity < entity; )
      pentry = (pstore = &pentry->p_next)->load( std::memory_order_acquire );

  // pstore указывает на атомарную переменную с указателем, на место которого будет вставка,
  // а pentry хранит его значение
    for ( ; ; )
    {
      newptr->p_next = pentry;

    // если найденный элемент больше добавляемого и не изменился, заместить его на новый
      if ( (pentry == nullptr || pentry->entity > entity) && pstore->compare_exchange_strong( pentry, newptr ) )
      {
        auto  curPoint = pindex.fetch_add( 1, std::memory_order_relaxed );

        return points[curPoint % 32] = pstore, void(ncount.fetch_add( 1, std::memory_order_relaxed ));
      }

    // если изменился, проверить, не стал ли он меньше вставляемого и не надо ли сделать
    // шаг дальше по списку
      if ( pentry != nullptr && pentry->entity < entity )
        pentry = (pstore = &pentry->p_next)->load( std::memory_order_acquire );
    }
  }

  template <class Allocator>
  bool  BlockChains<Allocator>::ChainHook::Verify() const
  {
    auto  entity = uint32_t(0);

    for ( auto pentry = pfirst.load(); pentry != nullptr; pentry = pentry->p_next.load() )
    {
      if ( pentry->entity <= entity )
        return false;
      entity = pentry->entity;
    }

    return true;
  }

  template <class Allocator>
  template <class OtherAllocator>
  auto  BlockChains<Allocator>::ChainHook::Remove( const Bitmap<OtherAllocator>& deleted ) -> ChainHook&
  {
    for ( auto p = pfirst.load(); p != nullptr; p = p->p_next.load() )
      if ( deleted.Get( p->entity ) )
      {
        p->entity = uint32_t(-1);
        --ncount;
      }
    return *this;
  }

  // BlockChains::KeyLister implementation

  template <class Allocator>
  BlockChains<Allocator>::KeyLister::KeyLister( std::shared_mutex& mx,
    const_it_type&& ib,
    const_it_type&& ie, std::string&& tp ):
      lck( mx ),
      beg( std::move( ib ) ),
      end( std::move( ie ) ),
      tpl( std::move( tp ) )
  {
    if ( tpl.length() != 0 )
    {
      int   rescmp;

      while ( beg != end && (rescmp = strmatch( beg->key, tpl )) < 0 )
        ++beg;
      if ( rescmp != 0 )
        beg = end;
    }
  }

  template <class Allocator>
  BlockChains<Allocator>::KeyLister::KeyLister( std::shared_mutex& mx,
    const_it_type&& ib,
    const_it_type&& ie ):
      lck( mx ),
      beg( std::move( ib ) ),
      end( std::move( ie ) ) {}

  template <class Allocator>
  auto  BlockChains<Allocator>::KeyLister::CurrentKey() -> std::string
  {
    return beg != end ? beg->key.to_string() : "";
  }

  template <class Allocator>
  auto  BlockChains<Allocator>::KeyLister::GetNextKey() -> std::string
  {
    if ( beg != end )
    {
      ++beg;

      if ( tpl.length() != 0 )
      {
        int   rescmp = 0;

        while ( beg != end && (rescmp = strmatch( beg->key, tpl )) < 0 )
          ++beg;

        if ( rescmp != 0 )
          beg = end;
      }
    }
    return beg != end ? beg->key.to_string() : "";
  }

}}}

# endif   // __structo_src_indexer_dynamic_chains_hxx__
