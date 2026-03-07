# include "../../context/x-contents.hpp"
# include "../../context/processor.hpp"
# include "../../context/fields-man.hpp"
# include <mtc/test-it-easy.hpp>

using namespace structo;
using namespace structo::context;

auto  StrKey( const char* str ) -> Key
{
  return { 0xff, codepages::mbcstowide( codepages::codepage_utf8, str ) };
}

TestItEasy::RegisterFunc  test_contents_processor( []()
{
  auto  proc = Processor();
  auto  text = DeliriX::Text();

  CopyUtf16( &text, DeliriX::Text{
    { "title", {
        "Сказ про радугу",
        "/",
        "Rainbow tales" } },
    { "body", {
        "Как однажды Жак Звонарь"
        "Городской сломал фонарь" } },
    "Очень старый фонарь" } );
  auto  body =
    proc.WordBreak( text );
    proc.SetMarkup( body, text );
    proc.Lemmatize( body );

  TEST_CASE( "context/contents" )
  {
    SECTION( "contents may be created" )
    {
      SECTION( "* as 'mini'" )
      {
        auto  fieldMan = FieldManager();
        auto  contents = Contents();

        if ( REQUIRE_NOTHROW( contents = MiniContents( body ) ) )
          if ( REQUIRE( contents.get().empty() == false ) )
          {
            for ( auto& next: contents.get() )
            {
              if ( REQUIRE( (next.bid == 0 || next.bid == 99) ) )
                REQUIRE( (next.bid == 0) == (next.val.size() == 0) );
            }
          }
      }
      SECTION( "* as 'BM25'" )
      {
        auto  fieldMan = FieldManager();
        auto  contents = Contents();

        if ( REQUIRE_NOTHROW( contents = BM25Contents( body ) ) )
          if ( REQUIRE( contents.get().empty() == false ) )
          {
            for ( auto& next: contents.get() )
            {
              if ( REQUIRE( (next.bid == 10 || next.bid == 99) ) )
                if ( REQUIRE( next.val.size() != 0 ) )
                {
                  auto  curkey = Key( next.key );
                  int   ncount;

                  ::FetchFrom( next.val.data(), ncount );

                  if ( curkey == StrKey( "фонарь" ) )
                  {
                    REQUIRE( ncount == 2 );
                  }
                    else
                  if ( curkey.size() == 3 && memcmp( curkey.data(), "dsr", 3 ) == 0 )
                  {
                    REQUIRE( ncount == 15 );
                  }
                    else
                  REQUIRE( ncount == 1 );
                }
            }
          }
      }
      SECTION( "* as 'Rich'" )
      {
        auto  fieldMan = FieldManager();
        auto  contents = Contents();

        if ( REQUIRE_NOTHROW( contents = GetRichContents( body.GetLemmas(), body.GetMarkup(), fieldMan ) ) )
          if ( REQUIRE( contents.get().empty() == false ) )
          {
            for ( auto& next: contents.get() )
            {
              if ( next.bid == 99 )
                continue;

              if ( next.bid == 20 && REQUIRE( next.val.size() != 0 ) )
              {
                int   getpos;
                auto  source = ::FetchFrom( next.val.data(), getpos );

                if ( Key( next.key ) == StrKey( "радугу" ) )
                {
                  REQUIRE( getpos == 2 );
                }
                  else
                if ( Key( next.key ) == StrKey( "фонарь" ) )
                {
                  REQUIRE( getpos == 11 );
                    ::FetchFrom( source, getpos );
                  REQUIRE( getpos == 2 );
                }
              }
            }
          }
      }
    }
  }
} );
