# if !defined( __structo_src_indexer_commit_contents_hxx__ )
# define __structo_src_indexer_commit_contents_hxx__
# include "../../contents.hpp"
# include "notify-events.hpp"

namespace structo {
namespace indexer {
namespace commit {

  struct Contents
  {
    auto  Create( mtc::api<IContentsIndex>, Notify::Func ) -> mtc::api<IContentsIndex>;
  };

}}}

# endif // !__structo_src_indexer_commit_contents_hxx__
