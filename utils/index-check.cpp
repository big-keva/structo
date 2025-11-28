# include "../storage/posix-fs.hpp"
# include <cstdio>
# include <indexer/layered-contents.hpp>
#include <indexer/static-contents.hpp>

int   CheckIndex( mtc::api<structo::IContentsIndex> index )
{
  auto  maxIndex = index->GetMaxIndex();
  auto  enBlocks = index->ListContents();

  fprintf( stdout, "index has %u entities\n", maxIndex );

  for ( auto key = enBlocks->Curr(); !key.empty(); key = enBlocks->Next() )
  {
    auto  keyStats = index->GetKeyStats( key );
    auto  keyBlock = index->GetKeyBlock( key );

    fprintf( stdout, "key '%s': type %u, count %u: ", key.c_str(), keyStats.bkType, keyStats.nCount );

    if ( strcmp( key.c_str(), "Ͻ." ) == 0 )
    {
      int i = 0;
    }
    if ( keyBlock == nullptr )
      return fputs( "index has no corresponding data!\n", stdout ), EFAULT;

    for ( auto entry = keyBlock->Find( 1 ); entry.uEntity != uint32_t(-1); entry = keyBlock->Find( entry.uEntity + 1) )
    {
      if ( entry.uEntity > maxIndex )
      {
        fprintf( stdout, "entity %u is out of range\n", entry.uEntity );

        if ( (entry = keyBlock->Find( entry.uEntity + 1 )).uEntity != uint32_t(-1) )
        {
          fprintf( stdout, "\tafter that it also refers %u", entry.uEntity );

          for ( int count = 0; count < 3 && (entry = keyBlock->Find( entry.uEntity + 1 )).uEntity != uint32_t(-1); ++count )
            fprintf( stdout, ", %u", entry.uEntity );

          return fprintf( stdout, "%s\n", entry.uEntity != uint32_t(-1) ? "..." : "" ), EFAULT;
        }
      }
    }

    fputs( "OK\n", stdout );
  }
  return 0;
}

int   CheckIndex( const char* path )
{
  auto  storage = structo::storage::posixFS::Open(
    structo::storage::posixFS::StoragePolicies::OpenInstance( path ) );
  auto  indices = storage->ListIndices();
  auto  inIndex = indices->Get();
  auto  toCheck = structo::indexer::static_::Index().Create( inIndex );

  return CheckIndex( toCheck );
}

const char about[] = "DX-check - theck the integrity of static index\n"
  "usage: %s index-root-name\n";

int   main( int argc, char* argv[] )
{
  auto  source = (const char*)nullptr;

  for ( int i = 1; i != argc; ++i )
    if ( source == nullptr )  source = argv[i];
      else return fprintf( stderr, "invalid parameter count, unexpected '%s'", argv[i] ), EINVAL;

  if ( source == nullptr )
    return fprintf( stdout, about, argv[0] ), 0;

  return CheckIndex( source );
}