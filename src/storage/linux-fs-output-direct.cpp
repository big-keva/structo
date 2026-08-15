#if !defined( _GNU_SOURCE )
#   define _GNU_SOURCE
# endif   // !_GNU_SOURCE

# include <mtc/exceptions.h>
# include <mtc/iStream.h>
# include <stdexcept>
# include <unistd.h>
# include <fcntl.h>
#include <mtc/bitset.h>
# include <sys/syscall.h>

# if 0 //def__NR_io_uring_setup
#   include <liburing.h>
# endif

namespace structo {
namespace storage {
namespace posixFS {

# if 0 // def __NR_io_uring_enter

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
    io_uring  ioring;           // io_uring init
    char*     buffer = nullptr;
    size_t    buflen;
    size_t    sublen;           // subbuffer size
    size_t    bcount;           // subbuffer count
    uint8_t   isBusy = 0;
    char*     bufptr;           // writing pointer
    int64_t   cbFile = 0;
    int       fileno;
  };

  // LinuxDirectOutput implementation

  LinuxDirectOutput::LinuxDirectOutput( const char* filepath, size_t ccbuffer )
  {
    auto    ualign = (ccbuffer + MemAlignDirectIO - 1) & ~(MemAlignDirectIO - 1);
    auto    utotal = ualign / MemAlignDirectIO;     // максимальное количество кусков
    auto    perOne = (utotal + 7) / 8;              // кусков в одном
    iovec   iobufs[8];

  // get io buffers
    sublen = perOne * MemAlignDirectIO;
    bcount = ccbuffer / sublen;
    buflen = bcount * sublen;

  //
    if ( (fileno = open( filepath, O_CREAT + O_WRONLY + O_DIRECT, 0600 )) < 0 )
      throw mtc::FormatError<mtc::file_error>( "Could not open file '%s'", filepath );

  // allocate the buffer
    if ( posix_memalign( (void**)&buffer, MemAlignDirectIO, buflen ) != 0 )
      throw std::bad_alloc();

    bufptr = buffer;

  // register buffers
    for ( size_t i = 0; i < bcount; ++i )
      iobufs[i] = { buffer + i * sublen, sublen };

  // max 8 чередующихся массивов
    io_uring_queue_init( std::min( 8U, unsigned(buflen / MemAlignDirectIO) ), &ioring, 0 );
    io_uring_register_buffers( &ioring, iobufs, bcount );
  }

  LinuxDirectOutput::~LinuxDirectOutput()
  {
    if ( buffer != nullptr )
      free( buffer );
    if ( fileno >= 0 )
      close( fileno );
    io_uring_queue_exit( &ioring );
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
      auto  curtag = (bufptr - buffer) / sublen;
      auto  curbeg = buffer + curtag * sublen;
      int   nerror;

      if ( bufptr != curbeg )
      {
        auto          sqe = io_uring_get_sqe( &ioring );
        io_uring_cqe* cqe;

        while ( sqe == nullptr )
        {
          io_uring_submit( &ioring );

          if ( io_uring_wait_cqe( &ioring, &cqe ) == 0 )
          {
            isBusy &= ~( 1 << io_uring_cqe_get_data64( cqe ) );
            io_uring_cqe_seen( &ioring, cqe );
          }
          sqe = io_uring_get_sqe( &ioring );
        }

        io_uring_prep_write_fixed( sqe, fileno, curbeg, (bufptr - curbeg + MemAlignDirectIO) & ~(MemAlignDirectIO - 1),
          cbFile, curtag );
        io_uring_sqe_set_data64( sqe, curtag );
        io_uring_submit( &ioring );

        cbFile += (bufptr - curbeg);
        isBusy |= (1 << curtag);
      }
      while ( isBusy != 0 )
      {
        io_uring_cqe* cqe;

        if ( io_uring_wait_cqe( &ioring, &cqe ) == 0 )
        {
          isBusy &= ~(1 << io_uring_cqe_get_data64( cqe ));
          io_uring_cqe_seen( &ioring, cqe );
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
      auto  curtag = (bufptr - buffer) / sublen;
      auto  curend = buffer + curtag * sublen + sublen;    // current write limit

      // copy next portion
      while ( srcptr != srcend && bufptr != curend )
        *bufptr++ = *srcptr++;

      // check if flush the buffer
      if ( bufptr == curend )
      {
        auto          sqe = io_uring_get_sqe( &ioring );
        uint32_t      tag = bcount;
        io_uring_cqe* cqe;

      // get writing slot
        while ( sqe == nullptr )
        {
          io_uring_submit( &ioring );

          if ( io_uring_wait_cqe( &ioring, &cqe ) == 0 )
          {
            isBusy &= ~(1 << io_uring_cqe_get_data64( cqe ));
            io_uring_cqe_seen( &ioring, cqe );
          }
          sqe = io_uring_get_sqe( &ioring );
        }

      // prepare writing buffer
        io_uring_prep_write_fixed( sqe, fileno, buffer + curtag * sublen, sublen, cbFile, curtag );
        io_uring_sqe_set_data64( sqe, curtag );
        io_uring_submit( &ioring );

      // correct writing info
        cbFile += sublen;
        isBusy |= (1 << curtag);

      // select unused segment
        while ( tag == bcount )
        {
          uint32_t  isFree = (1 << bcount) - 1;

        // Быстро разгребаем всё, что успело записаться
          while ( io_uring_peek_cqe( &ioring, &cqe ) == 0 )
          {
            isBusy &= ~(1 << io_uring_cqe_get_data64( cqe ));
              io_uring_cqe_seen( &ioring, cqe );
          }

        // Проверяем, есть ли свободный слот
          if ( (isFree &= ~isBusy) != 0 )
          {
            tag = mtc::bitset_first( isFree );
          }
            else
          if ( io_uring_wait_cqe( &ioring, &cqe ) == 0 )
          {
            isBusy &= ~(1 << (tag = io_uring_cqe_get_data64( cqe )));
            io_uring_cqe_seen( &ioring, cqe );
          }
        }

      // register write operation
        bufptr = buffer + (curtag = tag) * sublen;
        curend = bufptr + sublen;
      }
    }

    return srcptr - static_cast<const char*>( buf );
  }

  auto  CreateOutputStream( const char* filepath ) -> mtc::api<mtc::IByteStream>
  {
    return new LinuxDirectOutput( filepath, 0x80000 );
  }

# elif defined( O_DIRECT )
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
