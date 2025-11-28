# if !defined( __structo_indexer_static_contents_hpp__ )
# define __structo_indexer_static_contents_hpp__
# include "../contents.hpp"

namespace structo {
namespace indexer {
namespace static_ {

  struct Index
  {
    auto  Create( mtc::api<IStorage::ISerialized> ) -> mtc::api<IContentsIndex>;
  };

}}}

# endif   // !__structo_indexer_static_contents_hpp__
