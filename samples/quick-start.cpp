# include <structo/structo.hpp>
# include <mtc/exceptions.h>

using namespace structo;

# define  STORAGE_DIR "/var/tmp/"

void  FindDocuments(
  mtc::api<IContentsIndex>  index,
  const context::Processor& lproc,
  const char*               szreq )
{
  auto  query = queries::BuildMiniQuery( index, lproc, queries::ParseQuery( szreq ) );
  auto  docid = uint32_t(0);
  auto  found = uint32_t(0);

  fprintf( stdout, "search '%s'\n", szreq );

  if ( query == nullptr )
    return (void)fputs( "\tfound 0 entities\n", stdout );

  while ( (docid = query->SearchDoc( docid + 1 )) != uint32_t(-1) )
  {
    auto    tuples = query->GetTuples( docid );
    double  weight;

    if ( tuples.dwMode != tuples.None && (weight = rankers::BM25( tuples )) > 0.0 )
    {
      fprintf( stdout, "\t '%s', relevance %4.2f\n",
        std::string( index->GetEntity( docid )->GetId() ).c_str(), weight );

      ++found;
    }
  }
}

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

int   main( int argc, char* argv[] )
{
  auto  lingproc = context::Processor();

  try
  {
  // 1. create storage object with simple policy with single location for all files;
  // 2. create multi-slice index object
    auto  xStorage = storage::posixFS::Open( STORAGE_DIR "structo-quick-start" );
    auto  xManager = indexer::layered::Index( xStorage ).Create();

  // 3. Insert documents
    xManager->SetEntity( "doc-1", GetMiniContents( lingproc.MakeImage( DeliriX::Text{
      "Простой текст: документ из двух текстовых блоков.",
      "Каждый блок - строка." } ) ) );

    xManager->SetEntity( "doc-2", GetMiniContents( lingproc.MakeImage( DeliriX::Text{
      "Это второй документ из одного блока - строки." } ) ) );

    FindDocuments( xManager, lingproc, "Документ" );          // try find 'string block' (2 entities)

    FindDocuments( xManager, lingproc, "Второй & документ" ); // try find 'text string' (1 entity)

    FindDocuments( xManager, lingproc, "Простой | второй" );  // try find 'array | containing' (2 entities)

    xManager = nullptr;

    CleanFiles( STORAGE_DIR "structo-quick-start" "*" );

    return 0;
  }
  catch ( const mtc::file_error& xp )
  {
    return fprintf( stderr, "%s\n", xp.what() ), EFAULT;
  }
}
