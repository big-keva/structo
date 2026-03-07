# include "../../indexer/layered-contents.hpp"
# include "../../storage/posix-fs.hpp"
# include "../../compat.hpp"
# include "../toolbox/tmppath.h"
# include "../toolbox/dirtool.h"
# include <mtc/test-it-easy.hpp>
# include <mtc/zmap.h>
# include <thread>

using namespace structo;
using namespace structo::indexer;

class Contents:
  public std::vector<EntryView>,
  public std::map<std::string, std::string>
{
public:
  void  insert( std::string&& k, std::string&& v )
  {
    auto  ins = std::map<std::string, std::string>::insert( { std::move( k ), std::move( v ) } );

    if ( ins.second )
      emplace_back( ins.first->first, ins.first->second, 0U );
  }
};

auto  CreateContents() -> Contents
{
  Contents  out;

  auto  nKeys = size_t(100 + rand() * 300. / RAND_MAX);
  auto  nStep = size_t(1 + rand() * 2. / RAND_MAX);

  for ( size_t i = 0; i < nKeys; i += nStep )
    out.insert( mtc::strprintf( "key%u", i ), mtc::strprintf( "val%u", i ) );

  return out;
}

TestItEasy::RegisterFunc  stream_indexing( []()
  {
    RemoveFiles( GetTmpPath() + "k2" );

    TEST_CASE( "index/stream-indexing" )
    {
      auto  storage = storage::posixFS::Open( storage::posixFS::StoragePolicies::Open(
        GetTmpPath() + "k2" ) );
      auto  layered = layered::Index( storage )
        .Set( dynamic::Settings()
          .SetMaxEntities( 4096 )
          .SetMaxAllocate( 256 * 1024 * 1024 ) )
        .Create();

      SECTION( "indexing a set of entities generates a set of indices" )
      {
        std::vector<std::thread>  threads;
        auto                      kernels = std::thread::hardware_concurrency();

        for ( unsigned i = 0; i < kernels; ++i )
          threads.emplace_back( std::thread( [&]( int base )
          {
            pthread_setname_np( pthread_self(), "TestThread" );

            for ( auto entId = base * 1000; entId < (base + 1) * 1000; ++entId )
            {
              layered->SetEntity( std::string_view( mtc::strprintf( "ent%u", entId ) ),
                CreateContents() );
            }
          }, i ) );
        for ( auto& th: threads )
          th.join();
      }
      layered = nullptr;
      storage = nullptr;

      REQUIRE( SearchFiles( GetTmpPath() + "k2.*" ) );
      RemoveFiles( GetTmpPath() + "k2.*" );
    }
  } );
