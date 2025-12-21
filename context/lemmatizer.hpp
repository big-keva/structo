# if !defined( __structo_context_lemmatiser_hpp__ )
# define __structo_context_lemmatiser_hpp__
# include "../lang-api.hpp"

namespace structo {
namespace context {

  auto  LoadLemmatizer( const char*, const char* = nullptr ) -> mtc::api<ILemmatizer>;
  auto  LoadLemmatizer( const std::string&, const std::string& = {} ) -> mtc::api<ILemmatizer>;

}};

# endif // !__structo_context_lemmatiser_hpp__
