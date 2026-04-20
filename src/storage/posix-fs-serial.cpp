# include "../../storage/posix-fs.hpp"
# include "../../storage/posix-fs.hpp"
# include "../../compat.hpp"
# include "posix-fs-dump-store.hpp"
# include <mtc/exceptions.h>
# include <mtc/fileStream.h>
# include <mtc/wcsstr.h>
# include <mtc/json.h>
# include <stdexcept>
#include <mtc/bufStream.h>
#include <mtc/directory.h>

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

  /*
   * linux-specific implementation
   */
  auto  CreateOutputStream( const char* filepath ) -> mtc::api<mtc::IByteStream>;

  class Serialized final: public IStorage::ISerialized
  {
    implement_lifetime_control

    class Patch;

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

    auto  AddPatch() -> mtc::api<IPatch> override;
    void  SetPatch( IPatch* ) override;

  protected:
    const StoragePolicies             policies;

    mtc::api<const mtc::IByteBuffer>  entities;
    mtc::api<const mtc::IByteBuffer>  contents;
    mtc::api<IStorage::ICoordsRepo>   linkages;
    mtc::api<IStorage::IBundleRepo>   packages;

    mtc::zmap                         idxStats;

  };

  class Serialized::Patch final: public IPatch
  {
    mtc::api<mtc::IByteStream>  output;
    std::atomic_long            refCnt = 0;

    long  Attach() override;
    long  Detach() override;

    void  Delete( EntityId )                      override;
    void  Update( EntityId, const void*, size_t ) override;

  public:
    Patch( mtc::api<mtc::IByteStream> to ): output( to )  {}

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
    {
      try
      {
        entities = LoadByteBuffer( policies, Unit::entities );
      }
      catch ( const mtc::file_error& )  {}
    }
    return entities;
  }

  auto  Serialized::Contents() -> mtc::api<const mtc::IByteBuffer>
  {
    if ( contents == nullptr )
    {
      try
      {
        contents = LoadByteBuffer( policies, Unit::contents );
      }
      catch ( const mtc::file_error& )  {}
    }
    return contents;
  }

  auto  Serialized::Linkages() -> mtc::api<IStorage::ICoordsRepo>
  {
    if ( linkages == nullptr )
    {
      try
      {
        linkages = new BlocksRepo( OpenFileStream( policies.GetPolicy( Unit::linkages )->GetFilePath(
          Unit::linkages ).c_str(), O_RDONLY, mtc::enable_exceptions ) );
      }
      catch ( const mtc::file_error& )  {}
    }
    return linkages;
  }

  auto  Serialized::Packages() -> mtc::api<IStorage::IBundleRepo>
  {
    if ( packages == nullptr )
    {
      packages = CreateDumpStore( OpenFileStream( policies.GetPolicy( Unit::packages )->GetFilePath( Unit::packages ).c_str(),
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

    for ( auto unit: { Unit::entities, Unit::linkages, Unit::contents, Unit::packages, Unit::revision, Unit::bulletin } )
    {
      auto  policy = policies.GetPolicy( unit );

      if ( policy != nullptr )
      {
        auto  flpath = policy->GetFilePath( unit );

        if ( unit == Unit::revision )
        {
          auto  diread = mtc::directory::Open( (flpath + ".*").c_str(), mtc::directory::attr_file );

          for ( auto dirent = diread.Get(); dirent; dirent = diread.Get() )
            remove( mtc::strprintf( "%s%s", dirent.folder(), dirent.string() ).c_str() );
        }
          else
        remove( flpath.c_str() );
      }
    }
  }

 /*
  * capture new index patch for the index;
  * create output stream;
  * create patch object
  */
  auto  Serialized::AddPatch() -> mtc::api<IPatch>
  {
    auto  policy = policies.GetPolicy( revision );

    if ( policy == nullptr )
      throw std::logic_error( "invalid policy: undefined 'revision' @" __FILE__ ":" LINE_STRING );

    for ( auto  stempl = policy->GetFilePath( revision ); ; ) // base policy path
    {
      auto  uTimer = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch() ).count());
      auto  sPatch = stempl + mtc::strprintf( ".%lu", uTimer );
      auto  fPatch = open( sPatch.c_str(), O_CREAT + O_RDWR + O_EXCL, 0644 );
      int   nerror;

      if ( fPatch >= 0 )
        return close( fPatch ), new Patch( CreateOutputStream( sPatch.c_str() ) );

      if ( (nerror = errno) != EEXIST )
        throw mtc::file_error( mtc::strprintf( "could not create file '%s', error %d (%s)", sPatch.c_str(), nerror, strerror( nerror ) ) );
    }
  }

 /*
  * Serialized::SetPatch( IPatch* to )
  *
  * loads the list of patches, deserializes and calls the interface passed for each record
  */
  void  Serialized::SetPatch( IPatch* to )
  {
    auto  policy = policies.GetPolicy( revision );
    auto  diread = mtc::directory();
    auto  afiles = std::vector<std::string>();

    if ( to == nullptr )
      throw std::invalid_argument( "'to' parameter must not be nullptr @" __FILE__ ":" LINE_STRING );

    if ( policy == nullptr )
      throw std::logic_error( "invalid policy: undefined 'revision' @" __FILE__ ":" LINE_STRING );

    if ( !(diread = mtc::directory::Open( (policy->GetFilePath( revision ) + ".*").c_str(), mtc::directory::attr_file )).defined() )
      return;

    for ( auto dirent = diread.Get(); dirent; dirent = diread.Get() )
      afiles.emplace_back( mtc::strprintf( "%s%s", dirent.folder(), dirent.string() ) );

    if ( !afiles.empty() )  std::sort( afiles.begin(), afiles.end() );
      else return;

    for ( auto& next: afiles )
    {
      auto  source = mtc::OpenFileStream( next, O_RDONLY, mtc::enable_exceptions );
      auto  mapped = source->MemMap( 0, source->Size() );
      auto  bufptr = mapped->GetPtr();
      auto  bufend = mapped->GetPtr() + mapped->GetLen();

      while ( bufptr != nullptr && bufptr < bufend )
      {
        const char* keyptr;
        size_t      keylen;
        const char* valptr;
        size_t      vallen;
        char        opcode;

        // get key length
        if ( (keyptr = bufptr = ::FetchFrom( bufptr, keylen )) == nullptr || bufptr + keylen >= bufend )
          break;

        // get patch operation
        bufptr = ::FetchFrom( bufptr + keylen, opcode );

        // check operation type
        if ( opcode == 'D' )  to->Delete( { keyptr, keylen } );
          else
        if ( opcode != 'U' )  throw std::invalid_argument( "unexpected patch record type @" __FILE__ ":" LINE_STRING );

        if ( (valptr = bufptr = ::FetchFrom( bufptr, vallen )) == nullptr || bufptr + vallen > bufend )
          break;
        to->Update( { keyptr, keylen }, valptr, vallen );
          bufptr += vallen;
      }
    }
  }

  // Serialized::Patch

  long  Serialized::Patch::Attach()
  {
    return ++refCnt;
  }

  long  Serialized::Patch::Detach()
  {
    auto  rcount = --refCnt;

    if ( rcount == 0 )
    {
      output = nullptr;
      delete this;
    }
    return rcount;
  }

  void  Serialized::Patch::Delete( EntityId entid )
  {
    ::Serialize(
    ::Serialize(
    ::Serialize( output.ptr(), entid.size() ), entid.data(), entid.size() ), 'D' );
  }

  void  Serialized::Patch::Update( EntityId  entid, const void* mdata, size_t ldata )
  {
    ::Serialize(
    ::Serialize(
    ::Serialize(
    ::Serialize(
    ::Serialize( output.ptr(), entid.size() ), entid.data(), entid.size() ), 'U' ), ldata ), mdata, ldata );
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
