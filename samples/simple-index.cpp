# include <structo/structo.hpp>

using namespace structo;

# define  STORAGE_DIR "/var/tmp/"
# define  INDEX_NAME  "structo-simple-index"

void  CleanFiles( const char* mask )
{
  char  syscmd[0x400];

#ifdef _WIN32
  sprintf( syscmd, "del /Q %s 2>nul", mask );
#else
  sprintf( syscmd, "rm -f %s 2>/dev/null", mask );
#endif
  system( syscmd );
}

struct set_entity
{
  const char*                 id;
  mtc::span<const EntryView>  keys;
};

int   main()
{
// Create storage object with simple policy with single location for all files.
// Create multi-slice index object
  auto  indexStorage = storage::posixFS::Open( STORAGE_DIR INDEX_NAME );
  auto  indexManager = indexer::layered::Index( indexStorage ).Create();
  auto  entitiesList = mtc::api<IContentsIndex::IEntitiesList>();

// Store entities with keys
  for ( auto& ent: std::initializer_list<set_entity>{
    { "entity-2", { { "B" }, { "C" }, { "D" } } },
    { "entity-1", { { "A" }, { "B" }, { "C" } } },
    { "entity-3", { { "C" }, { "D" }, { "E" } } } } )
  {
    indexManager->SetEntity( ent.id, ent.keys );
  }

// List entities
  if ( (entitiesList = indexManager->ListEntities( 0U )) != nullptr )
  {
    fputs( "list entities by order:\n", stdout );

    for ( auto ent = entitiesList->Curr(); ent != nullptr; ent = entitiesList->Next() )
      fprintf( stdout, "\t%s\n", std::string( ent->GetId() ).c_str() );
  }

  if ( (entitiesList = indexManager->ListEntities( "" )) != nullptr )
  {
    fputs( "list entities by id:\n", stdout );

    for ( auto ent = entitiesList->Curr(); ent != nullptr; ent = entitiesList->Next() )
      fprintf( stdout, "\t%s\n", std::string( ent->GetId() ).c_str() );
  }

// Find entities by keys
  for ( auto key: std::initializer_list{ "0", "A", "B", "C", "D", "E" } )
  {
    auto  block = indexManager->GetKeyBlock( key );

    fprintf( stdout, "select '%s'\n", key );

    if ( block == nullptr ) fputs( "\tfound 0 entities\n", stdout );
      else
    for ( auto next = block->Find( 0 ); next.uEntity != uint32_t(-1); next = block->Find( next.uEntity + 1 ) )
      fprintf( stdout, "\t '%s'\n", std::string( indexManager->GetEntity( next.uEntity )->GetId() ).c_str() );
  }

  indexManager = nullptr;
    CleanFiles( STORAGE_DIR INDEX_NAME "*" );

  return 0;
}
