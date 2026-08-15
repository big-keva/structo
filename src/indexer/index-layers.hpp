# if !defined( __structo_src_indexer_index_layers_hpp__ )
# define __structo_src_indexer_index_layers_hpp__
#include <shared_mutex>

# include "../../contents.hpp"
# include "dynamic-bitmap.hpp"

namespace structo {
namespace indexer {

 /*
  * IndexLayers обеспечивает хранение фиксированного массива элементов
  * с индекесами и базовые примитивы работы с ними.
  *
  * Сами индексы хранятся как атомарные переменные и работа с ними
  * ведётся на уровне забрать-положить.
  *
  * Это даёт возможность подменять интерфейсы индексов на лету (commit,
  * merge), а также добавлять новые до достижения предела размерности.
  *
  * Ротацию таких массивов при переполнении будет обеспечивать другой
  * компонент.
  */
  class IndexLayers
  {
  public:
    IndexLayers() = default;
    IndexLayers( const mtc::api<IContentsIndex>*, size_t );
    IndexLayers( const std::vector<mtc::api<IContentsIndex>>& ppi ):
      IndexLayers( ppi.data(), ppi.size() ) {}

    auto  getEntity( EntityId ) const -> mtc::api<const IEntity>;
    auto  getEntity( uint32_t ) const -> mtc::api<const IEntity>;
    bool  delEntity( EntityId id );
    auto  setExtras( EntityId, const std::string_view& ) -> mtc::api<const IEntity>;

    auto  getMaxIndex() const -> uint32_t;
    auto  getKeyBlock( const std::string_view&, const mtc::Iface* = nullptr ) const -> mtc::api<IContentsIndex::IEntities>;
    auto  getKeyStats( const std::string_view& ) const -> IContentsIndex::BlockInfo;

    auto  listContents( const std::string_view&, const mtc::Iface* = nullptr ) -> mtc::api<IContentsIndex::IContentsList>;

    void  addContents( mtc::api<IContentsIndex> pindex );

    void  commitItems();

    void  hideClashes();

  protected:
    struct IndexEntry
    {
      uint32_t                  uLower;
      uint32_t                  uUpper;
      mtc::api<IContentsIndex>  pIndex;
      std::vector<IndexEntry>   backup;
      uint32_t                  dwSets = 0;

    public:
      IndexEntry( uint32_t uLower, mtc::api<IContentsIndex> pindex );
      IndexEntry( const IndexEntry& );
      IndexEntry& operator=( const IndexEntry& );

    public:
      auto  Override( mtc::api<const IEntity> ) const -> mtc::api<const IEntity>;

    };

    class ContentsList;

  protected:
    std::vector<IndexEntry> layers;

  };

  class IndexLayers::ContentsList final: public IContentsIndex::IContentsList
  {
    implement_lifetime_control

    ContentsList( const std::vector<mtc::api<IContentsList>>&, const Iface* );

  public:
    auto  Curr() -> std::string override;
    auto  Next() -> std::string override;

  protected:
    struct Iterator: protected mtc::api<IContentsList>
    {
      using api::api;

      std::string contentsItem;

      Iterator( const api& list ): api( list ), contentsItem( list->Curr() )  {}

    public:
      auto  Curr() -> const std::string&  {  return contentsItem;  }
      auto  Next() -> const std::string&  {  return contentsItem = ptr()->Next();  }
    };

    mtc::api<const Iface> parentObject;
    std::vector<Iterator> contentsList;
    const std::string*    currentValue = nullptr;


  };

  class EntitiesChain final: public IContentsIndex::IEntities
  {
    friend class IndexLayers;

    struct BlockEntry
    {
      uint32_t            uLower;
      uint32_t            uUpper;
      mtc::api<IEntities> entSet;
    };

    using BlockSet = std::vector<BlockEntry>;

    mtc::api<const Iface>             holder;
    BlockSet                          blocks;
    mutable BlockSet::const_iterator  pblock;
    uint32_t                          ncount = 0;
    uint32_t                          bktype = uint32_t(-1);

    implement_lifetime_control

  public:
    EntitiesChain( const Iface* parent = nullptr );

    static constexpr struct to_head_t{} to_head{};
    static constexpr struct to_tail_t{} to_tail{};

    void  AddBlock( const BlockEntry&, const to_head_t& );
    void  AddBlock( const BlockEntry&, const to_tail_t& );

    // overridables
    auto  Copy( const Bounds& ) const -> mtc::api<IEntities> override;
    auto  Find( uint32_t ) -> Reference override;
    auto  Last() const -> uint32_t override;
    auto  Size() const -> uint32_t override {  return ncount;  }
    auto  Type() const -> uint32_t override {  return bktype;  }

  };

}}

# endif   // !__structo_src_indexer_index_layers_hpp__
