# include "../../storage/posix-fs.hpp"
# include "../../compat.hpp"
# include "posix-fs-dump-store.hpp"
# include <mtc/exceptions.h>
# include <mtc/fileStream.h>
# include <mtc/wcsstr.h>
# include <mtc/json.h>
# include <stdexcept>

template <>
int*  FetchFrom( int* pfd, void* pv, size_t cc )
{
  auto  beg = (char*)pv;
  auto  end = (char*)pv + cc;
  int   cch;

  while ( pfd != nullptr && beg != end )
  {
    if ( (cch = read( *pfd, beg, end - beg )) > 0 )  beg += cch;
      else
    if ( errno != EAGAIN && errno != EWOULDBLOCK )  return nullptr;
  }
  return pfd;
}

namespace structo {
namespace storage {
namespace posixFS {

  class Serialized final: public IStorage::ISerialized
  {
    implement_lifetime_control

  public:
    Serialized( const StoragePolicies& );

  public:
    auto  Entities() -> mtc::api<const mtc::IByteBuffer> override;
    auto  Contents() -> mtc::api<const mtc::IByteBuffer> override;
    auto  Linkages() -> mtc::api<IStorage::ICoordsRepo> override;
    auto  Packages() -> mtc::api<IStorage::IBundleRepo> override;

    auto  GetStats() -> mtc::zmap override  {  return idxStats;  }

    auto  Commit() -> mtc::api<ISerialized> override;
    void  Remove() override;

    auto  NewPatch() -> mtc::api<IPatch> override;

  protected:
    const StoragePolicies             policies;

    mtc::api<const mtc::IByteBuffer>  entities;
    mtc::api<const mtc::IByteBuffer>  contents;
    mtc::api<IStorage::ICoordsRepo>   linkages;
    mtc::api<IStorage::IBundleRepo>   packages;

    mtc::zmap                         idxStats;

  };

  class BlocksRepo final: public IStorage::ICoordsRepo
  {
    mtc::api<mtc::IFileStream>  fileStream;

  public:
    BlocksRepo( const mtc::api<mtc::IFileStream>& in ):
      fileStream( in )  {}

    auto  Get( int64_t off, uint64_t len ) const -> mtc::api<const mtc::IByteBuffer> override;

    implement_lifetime_control
  };

  auto  LoadByteBuffer( const StoragePolicies& policies, Unit unit ) -> mtc::api<const mtc::IByteBuffer>
  {
    auto  policy = policies.GetPolicy( unit );

    if ( policy != nullptr )
    {
      auto  infile = mtc::OpenFileStream( policy->GetFilePath( unit ).c_str(), O_RDONLY,
        mtc::enable_exceptions );

    // check the signature

    // if preloaded, return preloaded buffer, else memory-mapped
      if ( policy->mode == preloaded )
      {
        if ( infile->Size() > (std::numeric_limits<uint32_t>::max)() )
          throw std::invalid_argument( "file too large to be preloaded @" __FILE__ ":" LINE_STRING );
        return infile->PGet( 0, uint32_t(infile->Size() - 0) ).ptr();
      }
      if ( policy->mode == memory_mapped )
        return infile->MemMap( 0, infile->Size() - 0 ).ptr();
      throw std::invalid_argument( "invalid open mode @" __FILE__ ":" LINE_STRING );
    }
    return nullptr;
  }

  // Serialized implementation

  Serialized::Serialized( const StoragePolicies& pol ):
    policies( pol )
  {
    auto  policy = policies.GetPolicy( bulletin );
    auto  stpath = std::string();
    int   handle;

    if ( policy == nullptr )
      throw std::logic_error( "invalid policy @" __FILE__ ":" LINE_STRING );

    if ( (handle = open( (stpath = policy->GetFilePath( bulletin )).c_str(), O_RDONLY )) < 0 )
    {
      throw mtc::FormatError<mtc::file_error>( "could not open file '%s', error %d (%s)",
        stpath.c_str(), errno, strerror( errno ) );
    }

    try
    {
      if ( mtc::json::Parse( &handle, idxStats ) == nullptr )
      {
        throw mtc::FormatError<std::invalid_argument>( "error reading file '%s', error %d (%s)",
          stpath.c_str(), errno, strerror( errno ) );
      }
    }
    catch ( ... )
    {
      close( handle );
      throw;
    }
    close( handle );
  }

  /*
  * Serialized::Entities()
  *
  * Loads and returns the byte buffer for entities table access.
  */
  auto  Serialized::Entities() -> mtc::api<const mtc::IByteBuffer>
  {
    if ( entities == nullptr )
      entities = LoadByteBuffer( policies, Unit::entities );
    return entities;
  }

  auto  Serialized::Contents() -> mtc::api<const mtc::IByteBuffer>
  {
    if ( contents == nullptr )
      contents = LoadByteBuffer( policies, Unit::contents );
    return contents;
  }

  auto  Serialized::Linkages() -> mtc::api<IStorage::ICoordsRepo>
  {
    if ( linkages == nullptr )
    {
      auto  infile = mtc::OpenFileStream( policies.GetPolicy( Unit::linkages )->GetFilePath( Unit::linkages ).c_str(),
        O_RDONLY, mtc::enable_exceptions );
      linkages = new BlocksRepo( infile );
    }
    return linkages;
  }

  auto  Serialized::Packages() -> mtc::api<IStorage::IBundleRepo>
  {
    if ( packages == nullptr )
    {
      packages = CreateDumpStore( mtc::OpenFileStream( policies.GetPolicy( Unit::packages )->GetFilePath( Unit::packages ).c_str(),
        O_RDONLY, mtc::disable_exceptions ).ptr() );
    }
    return packages;
  }

  auto  Serialized::Commit() -> mtc::api<ISerialized>
  {
    return this;
  }

  void  Serialized::Remove()
  {
    entities = nullptr;
    linkages = nullptr;
    contents = nullptr;
    packages = nullptr;

    for ( auto unit: { Unit::entities, Unit::linkages, Unit::contents, Unit::packages, Unit::bulletin } )
    {
      auto  policy = policies.GetPolicy( unit );

      if ( policy != nullptr )
        remove( policy->GetFilePath( unit ).c_str() );
    }
  }

  auto  Serialized::NewPatch() -> mtc::api<IPatch>
  {
    return nullptr;
  }

  // BlocksRepo implementation

  auto  BlocksRepo::Get( int64_t off, uint64_t len ) const -> mtc::api<const mtc::IByteBuffer>
  {
    return fileStream->MemMap( off, len ).ptr();
  }

  auto  OpenSerial( const StoragePolicies& policies ) -> mtc::api<IStorage::ISerialized>
  {
    return new Serialized( policies );
  }

}}}
