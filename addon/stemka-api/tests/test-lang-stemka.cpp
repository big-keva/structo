# include "../../../context/lemmatizer.hpp"
# include "../../../lang-api.hpp"
# include <mtc/test-it-easy.hpp>
# include <moonycode/codes.h>

using namespace structo;

# define __Q__(x) #x
# define QUOTE(x) __Q__(x)

struct LemmmasStub: public ILemmatizer::IWord
{
  implement_lifetime_stub

  LemmmasStub( std::string& s ): to( s )  {}

  void  AddTerm( uint32_t lex,
    float flp, const uint8_t* forms, size_t count ) override  {  throw std::logic_error("not implemented");  }
  void  AddStem( const widechar* pws, size_t len, uint32_t cls,
    float flp, const uint8_t* forms, size_t count ) override;

protected:
  std::string&  to;
};

void  LemmmasStub::AddStem( const widechar* pws, size_t len, uint32_t, float, const uint8_t*, size_t )
{
  to += to.empty() ? "" : "|";
  to += codepages::widetombcs( codepages::codepage_utf8, pws, len );
}

TestItEasy::RegisterFunc  test_stemka( []()
  {
    TEST_CASE( "addon/stemka-api" )
    {
      SECTION( "stemka-api library may be loaded" )
      {
        auto  stemka = mtc::api<ILemmatizer>();
        auto  sstems = mtc::charstr();
        auto  lemmas = LemmmasStub( sstems );

        if ( !REQUIRE_NOTHROW( stemka = context::LoadLemmatizer( QUOTE(STEMKA_SO_PATH), "" ) ) )
          break;
        if ( !REQUIRE( stemka != nullptr ) )
          break;

        SECTION( "called for text, it creates fuzzy keys for russian words" )
        {
          SECTION( "for invalid args it returns EINVAL" )
          {
            if ( REQUIRE_NOTHROW( stemka->Lemmatize( nullptr, u"" ) ) )
              REQUIRE( stemka->Lemmatize( nullptr, u"" ) == EINVAL );
            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, nullptr, 0 ) ) )
              REQUIRE( stemka->Lemmatize( &lemmas, nullptr, 0 ) == EINVAL );
            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, u"ррррррррррр"
              "ррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррр" ) ) )
            {
              REQUIRE( stemka->Lemmatize( &lemmas, u"ррррррррррр"
              "ррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррррр" ) == EOVERFLOW );
            }
          }
          SECTION( "lemmas are built for regular words" )
          {
            sstems.clear();

            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, u"собака" ) ) )
              REQUIRE( sstems == "собак" );

            sstems.clear();

            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, u"кошка" ) ) )
              REQUIRE( sstems == "кош|кошк" );

            sstems.clear();

            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, u"собравшихся" ) ) )
              REQUIRE( sstems == "собра|собравш" );
          }
          SECTION( "lemmas are not built for non-cyrillic sequences" )
          {
            sstems.clear();

            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, u"==========" ) ) )
              REQUIRE( sstems == "" );

            sstems.clear();

            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, u"linux" ) ) )
              REQUIRE( sstems == "" );

            sstems.clear();

            if ( REQUIRE_NOTHROW( stemka->Lemmatize( &lemmas, u"stragdущий" ) ) )
              REQUIRE( sstems == "stragdущ" );
          }
        }
      }
    }
  } );
