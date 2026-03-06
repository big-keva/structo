#if !defined( _GNU_SOURCE )
#   define _GNU_SOURCE
# endif   // !_GNU_SOURCE

# include <mtc/exceptions.h>
# include <mtc/iStream.h>
# include <fcntl.h>
# include <stdexcept>
# include <unistd.h>

namespace structo {
namespace storage {
namespace posixFS {

# if defined( O_DIRECT )
 /*
  * LinuxDirectOutput обеспечивает запись кусками прямо на диск, не сбивая
  * работу дискового кэша.
  *
  * Простой поток, только накопление и запись страницами.
  */
  class LinuxDirectOutput final: public mtc::IByteStream
  {
    static constexpr size_t MemAlignDirectIO = 4096;

    std::atomic_long  refCount = 0;

  public:
    LinuxDirectOutput( const char*, size_t buflen = 0x400 * 0x400 );
    LinuxDirectOutput( const std::string& sz, size_t buflen = 0x400 * 0x400 ):
      LinuxDirectOutput( sz.c_str(), buflen ) {}
   ~LinuxDirectOutput();

    long      Attach() override;
    long      Detach() override;

    uint32_t  Get(       void*, uint32_t ) override {  throw std::logic_error( "Not implemented" );  }
    uint32_t  Put( const void*, uint32_t ) override;

  protected:
    void*       buffer = nullptr;
    char*       bufptr;
    const char* bufend;
    int64_t     cbFile = 0;
    int         fileno;
  };

  // LinuxDirectOutput implementation

  LinuxDirectOutput::LinuxDirectOutput( const char* filepath, size_t buflen )
  {
    if ( (fileno = open( filepath, O_CREAT + O_WRONLY + O_DIRECT, 0600 )) < 0 )
      throw mtc::FormatError<mtc::file_error>( "Could not open file '%s'", filepath );

    if ( posix_memalign( (void**)&buffer, MemAlignDirectIO, buflen ) != 0 )
      throw std::bad_alloc();

    bufend = (bufptr = static_cast<char*>( buffer )) + buflen;
  }

  LinuxDirectOutput::~LinuxDirectOutput()
  {
    if ( buffer != nullptr )
      free( buffer );
    if ( fileno >= 0 )
      close( fileno );
  }

  long  LinuxDirectOutput::Attach()
  {
    return ++refCount;
  }

  long  LinuxDirectOutput::Detach()
  {
    long  rCount;

    // on object destruction, write file and truncate length
    if ( (rCount = --refCount) == 0 )
    {
      int   nerror;

      if ( bufptr != static_cast<char*>( buffer ) )
      {
        if ( write( fileno, buffer, bufend - static_cast<const char*>( buffer ) ) != bufend - static_cast<const char*>( buffer ) )
        {
          nerror = errno;

          throw mtc::FormatError<mtc::file_error>( "Error %d writing file ('%s')",
            nerror, strerror( nerror ) );
        }
      }
      if ( ftruncate( fileno, cbFile ) != 0 )
      {
        nerror = errno;

        throw mtc::FormatError<mtc::file_error>( "Error %d truncating file ('%s')",
          nerror, strerror( nerror ) );
      }
      if ( fdatasync( fileno ) != 0 )
      {
        nerror = errno;

        throw mtc::FormatError<mtc::file_error>( "Error %d sync file ('%s')",
          nerror, strerror( nerror ) );
      }

      delete this;
    }
    return rCount;
  }

  uint32_t  LinuxDirectOutput::Put( const void* buf, uint32_t len )
  {
    auto  srcptr = static_cast<const char*>( buf );
    auto  srcend = static_cast<const char*>( buf ) + len;

    while ( srcptr != srcend )
    {
      // check if flush the buffer
      if ( bufptr == bufend )
      {
        if ( write( fileno, buffer, bufend - static_cast<const char*>( buffer ) ) != bufend - static_cast<const char*>( buffer ) )
        {
          int   nerror = errno;

          throw mtc::FormatError<mtc::file_error>( "Error writing file, error %d ('%s')",
            nerror, strerror( nerror ) );
        }
        bufptr = static_cast<char*>( buffer );
      }

      // copy next portion
      while ( srcptr != srcend && bufptr != bufend )
        *bufptr++ = *srcptr++;
    }

    cbFile += len;

    return srcptr - static_cast<const char*>( buf );
  }

  auto  CreateOutputStream( const char* filepath ) -> mtc::api<mtc::IByteStream>
  {
    return new LinuxDirectOutput( filepath, 0x8000 );
  }

# else

  auto  CreateOutputStream( const char* filepath ) -> mtc::api<mtc::IByteStream>
  {
    return mtc::OpenBufStream( filepath, O_WRONLY, 0x8000, mtc::enable_exceptions );
  }

# endif

}}}
