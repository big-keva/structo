# include "../../storage/posix-fs.hpp"
# include "../../compat.hpp"
# include "posix-fs-dump-store.hpp"
# include <mtc/exceptions.h>
# include <mtc/fileStream.h>
# include <mtc/wcsstr.h>
# include <mtc/json.h>
# include <stdexcept>
# include <aio.h>
# include <sys/mman.h>
# include <sys/stat.h>

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
    class Mapping;
    class AioLoad;

    const size_t  ccpage = getpagesize();
    int64_t       cbfile;
    int           handle;

  public:
    BlocksRepo( int in ): handle( in )
    {
      struct stat64 fistat;

      if ( fstat64( in, &fistat ) == 0 )  cbfile = fistat.st_size;
        else throw mtc::file_error( "could not get file size" );
    }

    auto  Get( int64_t off, int64_t len ) const -> mtc::api<const mtc::IByteBuffer> override;

  protected:
    auto  Mapped( int64_t off, int64_t len ) const -> mtc::api<const mtc::IByteBuffer>;
    auto  AioGet( int64_t off, int64_t len ) const -> mtc::api<const mtc::IByteBuffer>;

    implement_lifetime_control
  };

  class BlocksRepo::Mapping final: public mtc::IByteBuffer
  {
    void*       mapped;
    const char* ptrtop;
    size_t      length;

  public:
    Mapping( void* mem, const char* ptr, size_t len ):
      mapped( mem ),
      ptrtop( ptr ),
      length( len ) {}
   ~Mapping()
      {
        munmap( mapped, ptrtop + length - (const char*)mapped );
      }
    const char* GetPtr() const override
      {  return ptrtop;  }
    size_t      GetLen() const override
      {  return length;  }
    int         SetBuf( const void*, size_t ) override
      {  throw std::logic_error( "not implemented" );  }
    int         SetLen( size_t ) override
      {  throw std::logic_error( "not implemented" );  }

    implement_lifetime_control
  };

  class BlocksRepo::AioLoad final: public mtc::IByteBuffer
  {
    aiocb               aioCtl;
    const size_t        length;
    mutable const char* buffer = nullptr;

  public:
    AioLoad( int fd, int64_t off, size_t len ): length( len )
      {
        memset( &aioCtl, 0, sizeof(aioCtl) );

        aioCtl.aio_fildes = fd;
        aioCtl.aio_buf = this + 1;
        aioCtl.aio_nbytes = len;
        aioCtl.aio_offset = off;
        aioCtl.aio_sigevent.sigev_notify = SIGEV_NONE;

        if ( aio_read( &aioCtl ) != 0 )
          throw mtc::file_error( "could not start aio reading" );
      }
    void  operator delete( void* p )
      {  delete [] (char*)p;  }
    const char* GetPtr() const override
      {
        if ( buffer == nullptr )
        {
          if ( aio_error( &aioCtl ) == EINPROGRESS )
          {
            auto  waitIt = &aioCtl;
            auto  msWait = timespec{ 0, 500 * 1000000 }; // 500ms

            if ( aio_suspend( &waitIt, 1, &msWait ) != 0 )
              throw mtc::file_error( "could not aio_read buffer data" );

            if ( aio_error( &aioCtl ) != 0 )
              throw mtc::file_error( "error aio_reading buffer data" );
          }
          buffer = (const char*)(this + 1);
        }
        return buffer;
      }
    size_t      GetLen() const override
      {  return length;  }
    int         SetBuf( const void*, size_t ) override
      {  throw std::logic_error( "not implemented" );  }
    int         SetLen( size_t              ) override
      {  throw std::logic_error( "not implemented" );  }
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
      auto  szpath = policies.GetPolicy( Unit::linkages )->GetFilePath( Unit::linkages );
      auto  infile = open( szpath.c_str(), O_RDONLY );

      if ( infile == -1 )
      {
        throw mtc::file_error( mtc::strprintf( "could not open file '%s', error %d (%s)",
          szpath.c_str(), errno, strerror( errno ) ) );
      }
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

  auto  BlocksRepo::Get( int64_t off, int64_t len ) const -> mtc::api<const mtc::IByteBuffer>
  {
    return len > 100 * 1024 * 1024 ?
      Mapped( off, len ) :
      AioGet( off, len );
  }

  auto  BlocksRepo::Mapped( int64_t off, int64_t len ) const -> mtc::api<const mtc::IByteBuffer>
  {
    auto  alignOff = (off / ccpage) * ccpage;
    auto  shiftOff = uint32_t(off - alignOff);
    auto  alignLen = size_t(len + shiftOff);

    auto  blockMap = mmap( NULL, alignLen, PROT_READ, MAP_PRIVATE, handle, alignOff );

    if ( blockMap == MAP_FAILED )
    {
      throw mtc::file_error( mtc::strprintf( "could not map file %d, error %d (%s)",
        handle, errno, strerror( errno ) ) );
    }
    return new Mapping( blockMap, shiftOff + (char*)blockMap, len );
  }

  auto  BlocksRepo::AioGet( int64_t off, int64_t len ) const -> mtc::api<const mtc::IByteBuffer>
  {
    auto  nalloc = len + sizeof(AioLoad);
    auto  palloc = new char[nalloc];

    return new( palloc ) AioLoad( handle, off, len );
  }

  auto  OpenSerial( const StoragePolicies& policies ) -> mtc::api<IStorage::ISerialized>
  {
    return new Serialized( policies );
  }

}}}
