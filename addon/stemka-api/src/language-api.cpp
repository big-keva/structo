# include "../../../lang-api.hpp"
# include <moonycode/codes.h>

# define EXPORTED_API __attribute__((__visibility__("default")))

void  Stematize( structo::ILemmatizer::IWord*, const widechar*, const char*, size_t );

struct Lemmatizer final: structo::ILemmatizer
{
  implement_lifetime_stub

/*
 * Lemmatize
 *
 * Creates text decomposition to lexemes for each word passed.
 * Has to be thread-safe, i.e. contain no read-write internal data.
 *
 */
  int   Lemmatize( IWord* out, const widechar* str, size_t len ) override
  {
    char      szword[0x40];    // no longer words in russian and ukrainian
    char*     pszout = szword;
    widechar  wsword[0x40];
    widechar* pwsout = wsword;

    if ( out == nullptr || str == nullptr )
      return EINVAL;

    if ( len == (size_t)-1 )
      for ( len = 0; str[len] != 0; ++len ) (void)NULL;

    if ( len >= 0x40 )
      return EOVERFLOW;

    for ( auto src = str, end = src + len; src != end; ++src )
      *pszout++ = codepages::__impl__::utf_1251<>::translate( *pwsout++ = codepages::xlatUtf16Lower[*src] );

    return Stematize( out, wsword, szword, len ), 0;
  }
};

extern "C"
{
  int   EXPORTED_API  CreateLemmatizer( structo::ILemmatizer** out, const char* /* cfg */ )
  {
    if ( out == nullptr )
      return EINVAL;
    (*out = new Lemmatizer())->Attach();
      return 0;
  }
}
