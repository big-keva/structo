# include "../storage/posix-fs.hpp"
# include <indexer/layered-contents.hpp>
# include <indexer/static-contents.hpp>
# include <mtc/wcsstr.h>
# include <cstdio>

int   PrintEntities( mtc::api<structo::IContentsIndex> index )
{
  auto  doc_it = index->ListEntities( "" );
  auto  prefix = "\n";

  fprintf( stdout, "[" );

  for ( auto doc = doc_it->Curr(); doc != nullptr; doc = doc_it->Next(), prefix = ",\n" )
  {
    auto  docid = std::string( doc->GetId() );
    auto  extra = doc->GetExtra();
    auto  pkpos = doc->GetBundlePos();
    auto  exstr = std::string();
    auto  pkstr = std::string();

    if ( extra != nullptr )
    {
      exstr = mtc::strprintf( ",\n"
        "    \"extra\": \"%u bytes\"", extra->GetLen() );
    }
    if ( pkpos != -1 )
    {
      pkstr = mtc::strprintf( ",\n"
        "    \"image\": %lu", pkpos );
    }
    fprintf( stdout, "%s"
      "  {\n"
      "    \"objid\": \"%s\",\n"
      "    \"index\": %u,\n"
      "    \"version\": %lu%s%s\n"
      "  }", prefix, docid.c_str(), doc->GetIndex(), doc->GetVersion(), exstr.c_str(), pkstr.c_str() );
  }

  fprintf( stdout, "\n]\n" );

  return 0;
}

int   PrintEntities( const char* path )
{
  auto  storage = structo::storage::posixFS::Open(
    structo::storage::posixFS::StoragePolicies::OpenInstance( path ) );
  auto  indices = storage->ListIndices();
  auto  inIndex = indices->Get();
  auto  toCheck = structo::indexer::static_::Index().Create( inIndex );

  return PrintEntities( toCheck );
}

const char about[] = "sx-stats - dump index statistics\n"
  "usage: %s -action index-root-name\n"
  "actions are:\n"
  "\t"  "-e[ntities] - dump index entities as json array;\n";

int   main( int argc, char* argv[] )
{
  auto  source = (const char*)nullptr;
  auto  action = (const char*)nullptr;

  for ( int i = 1; i != argc; ++i )
  {
    if ( argv[i][0] == '-' )
    {
      if ( action == nullptr )  action = argv[i] + 1;
        else return fprintf( stderr, "multiple action, unexpected '%s'\n", argv[i] ), EINVAL;
    }
      else
    if ( source == nullptr )  source = argv[i];
      else return fprintf( stderr, "invalid parameter count, unexpected '%s'\n", argv[i] ), EINVAL;
  }

  if ( source == nullptr )
    return fprintf( stdout, about, argv[0] ), 0;

  if ( action == nullptr )
    return fprintf( stdout, about, argv[0] ), 0;

  if ( strcasecmp( action, "e" ) == 0 || strcasecmp( action, "entities" ) == 0 )
    return PrintEntities( source );

  return fprintf( stderr, "unknown action '%s'\n", action ), EINVAL;
}
