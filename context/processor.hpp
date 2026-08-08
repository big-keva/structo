# if !defined( __structo_context_processor_hpp__ )
# define __structo_context_processor_hpp__
# include "../lang-api.hpp"
# include "DeliriX/DOM-text.hpp"
# include "text-image.hpp"
# include "fields-man.hpp"
# include <moonycode/chartype.h>
# include <moonycode/codes.h>
# include <mtc/arbitrarymap.h>

namespace structo {
namespace context {

  class Processor
  {
    using MarkupTag = DeliriX::MarkupTag;
    using ITextView = DeliriX::ITextView;

    struct Lemmatizer
    {
      unsigned               langId;
      mtc::api<ILemmatizer>  module;
    };

    template <class Allocator>  class InsertTerms;

  public:
    enum: size_t
    {
      max_word_length = 64
    };

    struct            as_wildcard_t {};
    static constexpr  as_wildcard_t as_wildcard{};

  template <class Allocator>
    auto  Lemmatize( BaseImage<Allocator>& ) const -> BaseImage<Allocator>&;
  template <class Allocator>
    auto  Lemmatize( std::vector<Lexeme, Allocator>&, wide_string_view ) const -> std::vector<Lexeme, Allocator>&;
  template <class Allocator>
    auto  MakeImage( BaseImage<Allocator>&, const ITextView&, const FieldHandler* = nullptr ) const -> BaseImage<Allocator>&;
  template <class Allocator>
    auto  SetMarkup( BaseImage<Allocator>&, const ITextView& ) const -> BaseImage<Allocator>&;
  template <class Allocator>
    auto  WordBreak( BaseImage<Allocator>&, const ITextView&, const FieldHandler* = nullptr ) const -> BaseImage<Allocator>&;

    auto  Lemmatize( const mtc::widestr& ) const -> std::vector<Lexeme>;
    auto  Lemmatize( const mtc::widestr&, const as_wildcard_t& ) const -> std::vector<Lexeme>;
    auto  MakeImage( const ITextView&, const FieldHandler* = nullptr ) const -> Image;
    auto  WordBreak( const ITextView&, const FieldHandler* = nullptr ) const -> Image;

  public:
    auto  AddModule( unsigned langId, const mtc::api<ILemmatizer>& ) -> Processor&;
    auto  Initialize( const mtc::span<const std::pair<unsigned, const mtc::api<ILemmatizer>>>& ) ->Processor&;

  protected:
    static  bool  IsPunct( widechar c )
    {
      return (codepages::charType[c] & 0xf0) == codepages::cat_P
          || (codepages::charType[c] & 0xf0) == codepages::cat_S;
    }
    void  MapMarkup( mtc::span<MarkupTag>,
      const mtc::span<const TextToken>& ) const;
    auto  Normalize( DeliriX::Text&, const DeliriX::ITextView& ) const -> const DeliriX::ITextView*;

  protected:
    std::vector<Lemmatizer>  languages;

  };

  template <class Allocator>
  class Processor::InsertTerms: public ILemmatizer::IWord
  {
    implement_lifetime_stub

    void  AddTerm( uint32_t lex, float, const uint8_t* forms, size_t count ) override
    {
      lemmas.emplace_back( langId, lex );
      lemmas.back().GetForms().set( forms, count );
    }
    void  AddStem( const widechar* pws, size_t len, uint32_t cls, float rng, const uint8_t* forms, size_t count ) override
    {
      (void)rng;
      lemmas.emplace_back( langId, cls, pws, len, lemmas.get_allocator() );
      lemmas.back().GetForms().set( forms, count );
    }

  public:
    InsertTerms( std::vector<Lexeme, Allocator>& terms, unsigned ilang ):
      lemmas( terms ),
      langId( ilang ) {}
    auto  ptr() const -> IWord* {  return (IWord*)this;  }

  protected:
    std::vector<Lexeme, Allocator>& lemmas;
    unsigned                        langId;

  };

  // Processor template implementation

  template <class Allocator>
  auto  Processor::SetMarkup( BaseImage<Allocator>& image, const ITextView& input ) const -> BaseImage<Allocator>&
  {
    image.markup.insert( image.markup.end(),
      input.GetMarkup().begin(), input.GetMarkup().end() );

    MapMarkup( { image.markup.data(), image.markup.size() }, image.GetTokens() );

    if ( image.markup.size() > 1 )
      image.markup.resize( std::unique( image.markup.begin(), image.markup.end() ) - image.markup.begin() );

    return image;
  }

  template <class Allocator>
  auto  Processor::Lemmatize( BaseImage<Allocator>& image ) const -> BaseImage<Allocator>&
  {
    struct StrRef
    {
      StrRef*           pnext;
      const TextToken*  pword;
      unsigned          index;
    };
    std::vector<StrRef>   items( image.tokens.size() );
    StrRef*               plast = items.data();
    std::vector<StrRef*>  itMap(
      image.tokens.size() < 2003 ? 3001 :
      image.tokens.size() < 8009 ? 12007 :
      image.tokens.size() < 16001 ? 20011 :
      image.tokens.size() < 28001 ? 32003 :
      image.tokens.size() < 55001 ? 60013 : 90031 );

    image.lemmas.clear();
    image.lemmas.resize( image.tokens.size() );
    image.lexbuf.reserve( image.tokens.size() * 2 );

  // create words index
    for ( size_t i = 0; i < image.tokens.size(); i++ )
    {
      auto& rfword = image.tokens[i];
      auto  dwhash = rfword.IsRational() ? std::hash<double>()( rfword.dvalue ) :
        std::hash<std::basic_string_view<widechar>>()( rfword.GetWideStr() );
      auto  hindex = dwhash % itMap.size();
      auto  pfound = itMap[hindex];

    // search for already lemmatized token
      while ( pfound != nullptr && *pfound->pword != rfword )
        pfound = pfound->pnext;

      if ( pfound == nullptr )
      {
        auto  curlen = image.lexbuf.size();

        if ( rfword.IsRational() )
          image.lexbuf.emplace_back( rfword.dvalue >= 0 ? 0xfe : 0xfd, rfword.dvalue );
        else
          Lemmatize( image.lexbuf, rfword.GetWideStr() );

        new( &image.lemmas[i] ) mtc::span<Lexeme>( (Lexeme*)curlen,
          image.lexbuf.size() - curlen );

        itMap[hindex] = new( plast++ ) StrRef{ itMap[hindex], &rfword, unsigned(i) };
      }
        else
      image.lemmas[i] = image.lemmas[pfound->index];
    }

  // transform indexes to pointers
    for ( auto& l: image.lemmas )
      l = { image.lexbuf.data() + size_t(l.data()), l.size() };

    return image;
  }

  template <class Allocator>
  auto  Processor::Lemmatize( std::vector<Lexeme, Allocator>& buf, wide_string_view str ) const -> std::vector<Lexeme, Allocator>&
  {
    auto  curlen = buf.size();

  // lemmatize with language modules in dictionary mode
    for ( auto& lang: languages )
      lang.module->Lemmatize( InsertTerms( buf, lang.langId ).ptr(), lex_lemma, str.data(), str.size() );

    if ( buf.size() == curlen )
    {
      buf.push_back( Lexeme( 0xff, codepages::strtolower( str.data(), str.size() ), buf.get_allocator() ) );

      for ( auto& lang: languages )
        lang.module->Lemmatize( InsertTerms( buf, lang.langId ).ptr(), lex_fuzzy, str.data(), str.size() );
    }

    return buf;
  }

 /*
  * word breaker
  *
  * stores words with context flags, pointer, offset and length to output array
  */
  template <class Allocator>
  auto  Processor::WordBreak( BaseImage<Allocator>& body, const ITextView& input,
    const FieldHandler* fdset ) const -> BaseImage<Allocator>&
  {
    auto  wcText = DeliriX::Text();
    auto  inView = Normalize( wcText, input );
    auto  nonBrk = std::vector<uint64_t>();
    auto  offset = uint32_t(0);

    body.clear();

    // create non-breakable limits
    if ( fdset != nullptr )
      for ( auto& markup: inView->GetMarkup() )
      {
        auto  pfield = fdset->Get( markup.tagKey );

        if ( pfield != nullptr )
        {
          if ( (pfield->options & FieldOptions::ofNoBreakWords) != 0 )
            mtc::bitset_set( nonBrk, { markup.uLower, markup.uUpper } );
          else
            mtc::bitset_del( nonBrk, { markup.uLower, markup.uUpper } );
        }
      }

    // list all blocks
    for ( auto beg = inView->GetBlocks().begin(); beg != inView->GetBlocks().end(); offset += (beg++)->GetTextSize() )
    {
      auto  sblock = beg->GetWideStr();
      auto  buforg = body.AddBuffer( sblock.data(), sblock.size() );
      auto  ptrtop = buforg;
      auto  ptrend = ptrtop + sblock.size();

      if ( sblock.empty() )
      {
        if ( auto pval = beg->GetNumeric(); pval != nullptr )
          body.GetTokens().emplace_back( TextToken::is_first, *pval, offset, beg->GetTextSize() );
        else throw std::invalid_argument( "Processor::WordBreak(...) can process only utf16 texts @" __FILE__ ":" LINE_STRING );
      }
        else
      for ( unsigned uFlags = TextToken::is_first; ptrtop != ptrend; uFlags = 0 )
      {
        // detect lower space
        if ( codepages::IsBlank( *ptrtop ) )
        {
          uFlags |= TextToken::lt_space;

          for ( ++ptrtop; ptrtop != ptrend && codepages::IsBlank( *ptrtop ); ++ptrtop )
            (void)NULL;
        }

        // select next word
        if ( ptrtop != ptrend )
        {
          auto  origin = ptrtop;

          // check non-breakable limits
          if ( mtc::bitset_get( nonBrk, uint32_t(offset + (ptrtop - buforg)) ) )
          {
            do ++ptrtop;
              while ( ptrtop != ptrend && mtc::bitset_get( nonBrk, uint32_t(offset + (ptrtop - origin)) ) );
          }
            else
          // get substring length
          if ( IsPunct( *ptrtop++ ) )
          {
            uFlags |= TextToken::is_punct;
          }
            else
          // select next word
          {
            while ( ptrtop != ptrend && !codepages::IsBlank( *ptrtop ) && !IsPunct( *ptrtop ) )
              ++ptrtop;
          }

          // create word string
          body.GetTokens().emplace_back( uFlags, origin,
            uint32_t(offset + origin - buforg), uint32_t(ptrtop - origin) );
        }
      }
    }
    return body;
  }

  template <class Allocator>
  auto  Processor::MakeImage( BaseImage<Allocator>& body, const ITextView& text,
    const FieldHandler* fdset ) const -> BaseImage<Allocator>&
  {
    return SetMarkup( Lemmatize( WordBreak( body, text, fdset ) ), text );
  }

}}

# endif // !__structo_context_processor_hpp__
