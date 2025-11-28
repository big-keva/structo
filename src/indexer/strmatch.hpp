# if !defined( __structo_src_indexer_strmatch_hpp__ )
# define __structo_src_indexer_strmatch_hpp__
# include <mtc/radix-tree.hpp>
# include <string>

namespace structo {
namespace indexer {

  int   strmatch( const mtc::radix::key&, const std::string& tpl );

}}

# endif   // !__structo_src_indexer_strmatch_hpp__
