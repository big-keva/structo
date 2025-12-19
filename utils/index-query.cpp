# include "../storage/posix-fs.hpp"
# include "../queries/parser.hpp"
# include "../queries/builder.hpp"
# include "../context/fields-man.hpp"
# include <indexer/static-contents.hpp>
# include <mtc/wcsstr.h>
# include <cstdio>

class DefaultFields: public structo::FieldHandler
{
  structo::FieldOptions defaultOptions;

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
    {  return index < 10000 ? &defaultOptions : nullptr;  }
};

auto  DefaultMappings() -> std::shared_ptr<structo::FieldHandler>
{
  return std::shared_ptr<structo::FieldHandler>( new DefaultFields() );
}

int   PrintObjects( mtc::api<structo::IContentsIndex> index, const char* query )
{
  auto  zquery = structo::queries::ParseQuery( codepages::codepage_utf8, query );
  auto  fields = DefaultMappings();
  auto  lgproc = structo::context::Processor();
  auto  iquery = structo::queries::BuildRichQuery( zquery, {}, index, lgproc, *fields.get() );

  if ( iquery == nullptr )
    fprintf( stdout, "nothing found\n" ), 0;

  for ( auto uid = 0; (uid = iquery->SearchDoc( uid + 1 )) != uint32_t(-1); )
  {
    auto  ranked = iquery->GetTuples( uid );

    if ( ranked.dwMode == structo::queries::Abstract::Rich && ranked.entries.size() != 0 )
    {
      auto  getdoc = index->GetEntity( uid );

      if ( getdoc != nullptr )
      {
        auto  sdocid = std::string( getdoc->GetId() );

        fprintf( stdout, "%u:\t%s\n", uid, sdocid.c_str() );
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
  "usage: %s index-root-name query\n";

int   main( int argc, char* argv[] )
{
  auto  source = (const char*)nullptr;
  auto  squery = (const char*)nullptr;

  for ( int i = 1; i != argc; ++i )
  {
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
