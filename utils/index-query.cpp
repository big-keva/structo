# include "../storage/posix-fs.hpp"
# include "../queries/parser.hpp"
# include "../queries/builder.hpp"
# include "../context/fields-man.hpp"
# include "../context/lemmatizer.hpp"
# include "../enquote/quotations.hpp"
# include "../indexer/static-contents.hpp"
# include <DeliriX/DOM-dump.hpp>
# include <mtc/wcsstr.h>
# include <cstdio>
# include <zlib.h>

class DefaultFields: public structo::FieldHandler
{
  mutable structo::FieldOptions defaultOptions;

public:
  DefaultFields()
  {
    defaultOptions.id = 0;
    defaultOptions.name = "default";
  }
  auto  Add( const std::string_view& ) -> structo::FieldOptions* override
    {  throw std::logic_error( "FieldHandler::Add() must not be called here" );  }
  auto  Get( const std::string_view& ) const -> const structo::FieldOptions* override
    {  return &defaultOptions;  }
  auto  Get( unsigned index ) const -> const structo::FieldOptions* override
  {
    if ( index > 1000 )
      return nullptr;
    return defaultOptions.id = index, &defaultOptions;
  }
};

auto  DefaultMappings() -> std::shared_ptr<structo::FieldHandler>
{
  return std::shared_ptr<structo::FieldHandler>( new DefaultFields() );
}

auto  lgproc = structo::context::Processor();

auto  Unpack( const mtc::span<const char>& src ) -> std::vector<char>
{
  std::vector<char> unpack( src.size() * 2 );
  uLongf            length;
  int               nerror;

  while ( (nerror = uncompress( (Bytef*)unpack.data(), &(length = unpack.size()),
    (const Bytef*)src.data(), src.size() )) == Z_BUF_ERROR )
    unpack.resize( unpack.size() * 3 / 2 );

  if ( nerror == Z_OK ) unpack.resize( length );
  else unpack.clear();

  return unpack;
}

auto  GetQuotation(
  structo::enquote::QuotesFunc      quoter,
  mtc::api<const structo::IEntity>  entity,
  const structo::queries::Abstract& quotes ) -> DeliriX::Text
{
  auto  bundle = entity->GetBundle();
  auto  quoted = DeliriX::Text();

  if ( bundle != nullptr )
  {
    auto  dump = mtc::zmap( mtc::zmap::dump( bundle->GetPtr() ) );
    auto  mkup = dump.get_array_char( "ft" );
    auto  buff = std::vector<char>();
    auto  text = mtc::span<const char>();

    // if image is packed, unpack it, else use plain image
    if ( dump.get_array_char( "im" ) != nullptr ) text = *dump.get_array_char( "im" );
      else
    if ( dump.get_array_char( "ip" ) != nullptr ) text = buff = Unpack( *dump.get_array_char( "ip" ) );

    if ( !text.empty() )
      quoter( &quoted, text, *mkup, quotes );
  }
  return quoted;
}

int   PrintObjects( mtc::api<structo::IContentsIndex> index, const char* query )
{
  auto  zquery = structo::queries::ParseQuery( codepages::codepage_utf8, query );
  auto  fields = DefaultMappings();
  auto  quotes = structo::enquote::QuoteMachine( *fields.get() );
  auto  quoter = quotes.Structured();
  auto  iquery = structo::queries::BuildRichQuery( zquery, {}, index, lgproc, *fields.get() );

  if ( iquery == nullptr )
    return fprintf( stdout, "nothing found\n" ), 0;

  for ( uint32_t uid = 201484; (uid = iquery->SearchDoc( uid + 1 )) != uint32_t(-1); )
  {
    auto  ranked = iquery->GetTuples( uid );

    if ( ranked.dwMode == structo::queries::Abstract::Rich && ranked.entries.size() != 0 )
    {
      auto  getdoc = index->GetEntity( uid );

      if ( getdoc != nullptr )
      {
        auto  sdocid = std::string( getdoc->GetId() );
        auto  quoted = GetQuotation( quoter, getdoc, ranked );

        fprintf( stdout, "%u:\t%s\n", uid, sdocid.c_str() );

        quoted.Serialize( DeliriX::dump_as::Json( DeliriX::dump_as::MakeOutput( stdout ) ) );

        fputc( '\n', stdout );
      }
    }
  }
  return 0;
}

int   PrintObjects( const char* path, const char* query )
{
  auto  storage = structo::storage::posixFS::Open(
    structo::storage::posixFS::StoragePolicies::OpenInstance( path ) );
  auto  indices = storage->ListIndices();
  auto  inIndex = indices->Get();
  auto  toCheck = structo::indexer::static_::Index().Create( inIndex );

  return PrintObjects( toCheck, query );
}

const char about[] = "sx-query - execute a query on an index\n"
  "usage: %s [options] index-root-name query\n"
  "options are:\n"
  "\t" "-lang:N:P - add language support module with id=N and library path 'P'";

int   main( int argc, char* argv[] )
{
  auto  source = (const char*)nullptr;
  auto  squery = (const char*)nullptr;

  for ( int i = 1; i != argc; ++i )
  {
    if ( *argv[i] == '-' )
    {
      if ( strncmp( argv[i], "-lang=", 6 ) == 0
        || strncmp( argv[i], "-lang:", 6 ) == 0 )
      {
        char* pcolon;
        auto  idlang = strtoul( argv[i] + 6, &pcolon, 10 );

        if ( *pcolon++ == ':' )
        {
          auto  module = structo::context::LoadLemmatizer( pcolon );

          lgproc.AddModule( idlang, module );
        }
          else
        return fprintf( stderr, "invalid language option '%s', N:P expected\n", argv[i] + 6 );
      }
        else
      return fprintf( stderr, "Unknown option '%s'\n", argv[i] ), EINVAL;
    }
      else
    if ( source == nullptr )  source = argv[i];
      else
    if ( squery == nullptr )  squery = argv[i];
      else
    return fprintf( stderr, "invalid parameter count, unexpected '%s'\n", argv[i] ), EINVAL;
  }

  if ( source == nullptr )
    return fprintf( stdout, about, argv[0] ), 0;

  if ( squery == nullptr )
    return fprintf( stdout, about, argv[0] ), 0;

  return PrintObjects( source, squery );
}
