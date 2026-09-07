# if !defined( __structo_src_indexer_merger_hpp__ )
# define __structo_src_indexer_merger_hpp__
# include "../../contents.hpp"
# include <functional>

namespace structo {
namespace indexer {
namespace fusion {

  class ContentsMerger
  {
    std::function<bool()>           canContinue = [](){  return true;  };
    std::function<void( uint32_t )> onWriteSize = {};

  public:
    ContentsMerger() = default;

    auto  Add( mtc::api<IContentsIndex> ) -> ContentsMerger&;
    auto  Set( std::function<bool()> ) -> ContentsMerger&;
    auto  Set( std::function<void( uint32_t )> ) -> ContentsMerger&;
    auto  Set( mtc::api<IStorage::IIndexStore> ) -> ContentsMerger&;
    auto  Set( const mtc::api<IContentsIndex>*, size_t ) -> ContentsMerger&;
    auto  Set( const std::vector<mtc::api<IContentsIndex>>& ) -> ContentsMerger&;
    auto  Set( const std::initializer_list<const mtc::api<IContentsIndex>>& ) -> ContentsMerger&;

    auto  operator()() -> mtc::api<IStorage::ISerialized>;

  protected:
    void  MergeEntities();
    void  MergeContents();

  protected:
    struct IndexRec
    {
      mtc::api<IContentsIndex>  index;
      std::vector<uint32_t>     remap;
      uint32_t                  oldId = 0;
      bool                      fixed = true;     // if renumeration order is stable

      IndexRec( mtc::api<IContentsIndex> ix ):
        index( ix ),
        remap( ix->GetMaxIndex() + 1 )  {}
    };
    mtc::api<IStorage::IIndexStore> storage;
    std::vector<IndexRec>           indices;
    mtc::zmap                       statMap{ { "created-by", "index-merger" } };

  };

}}}

# endif // !__structo_src_indexer_merger_hpp__
