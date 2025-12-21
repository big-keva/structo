# if !defined( __structo_lang_api_hpp__ )
# define __structo_lang_api_hpp__
# include "contents.hpp"

namespace structo {

  struct languageId final
  {
    constexpr static  unsigned russian   = 0;
    constexpr static  unsigned ukrainian = 1;
    constexpr static  unsigned english   = 2;

    constexpr static  unsigned hieroglyph = 0xff;
  };

  enum LexemeFlags: unsigned
  {
    lex_none  = 0,
    lex_lemma = 0x01,
    lex_fuzzy = 0x02
  };

  struct ILemmatizer: mtc::Iface
  {
    struct IWord: mtc::Iface
    {
      virtual void  AddTerm( uint32_t lex,
        float flp, const uint8_t* forms, size_t count ) = 0;
      virtual void  AddStem( const widechar* pws, size_t len, uint32_t cls,
        float flp, const uint8_t* forms, size_t count ) = 0;
    };

    virtual int   Lemmatize( IWord*, unsigned /* flags */, const widechar*, size_t ) = 0;

  public:     // wrappers
    int   Lemmatize( IWord* out, unsigned opt, const std::basic_string_view<widechar>& str )
      {  return Lemmatize( out, opt, str.data(), str.size() );  }
  };

  typedef int  (*CreateLemmatizer)( ILemmatizer**, const char* );

};

# endif // !__structo_lang_api_hpp__
