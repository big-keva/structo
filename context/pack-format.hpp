# if !defined( __structo_context_pack_format_hpp__ )
# define __structo_context_pack_format_hpp__
# include "ranker-tag.hpp"
# include "../fields.hpp"
# include "../compat.hpp"
# include <mtc/span.hpp>
# include <mtc/iStream.h>
# include <functional>
# include <DeliriX/text-API.hpp>

namespace structo {
namespace context {
namespace formats {

  using MarkupTag = DeliriX::MarkupTag;
  using RankerTag = context::RankerTag;

  class FormatBox
  {
    struct TLevel: RankerTag
    {
      const char* fmtptr;
      const char* fmtend;

      auto  Set( const char*, unsigned, const char* = nullptr ) -> TLevel&;
    };

    TLevel  levels[0x40];
    int     nlevel = -1;

  public:
    class iterator;

    FormatBox( const mtc::span<const char>& );

   /*
    * Get( unsigned pos )
    *
    * returns format id for pos specified, assumes incremental position order
    */
    auto  Get( unsigned ) -> unsigned;
   /*
    * Iterator
    */
    auto  begin() -> iterator;
    auto  end() -> iterator;
  };

  class FormatBox::iterator
  {
    friend class FormatBox;

    RankerTag   format = { unsigned(-1), unsigned(-1), unsigned(-1), unsigned(-1) };
    FormatBox*  fmtbox = nullptr;

    iterator( FormatBox* );
  public:
    iterator() = default;
    iterator( const iterator& );
    iterator& operator = ( const iterator& );

  // conversions
    auto  operator -> () const -> const RankerTag*  {  return &format;  }
    auto  operator * () const -> const RankerTag&   {  return  format;  }

  // operators
    bool  operator == ( const iterator& it ) const  {  return format == it.format;  }
    bool  operator != ( const iterator& it ) const  {  return !operator == ( it );  }
    auto  operator ++ () -> iterator&;
    auto  operator ++ ( int ) -> iterator;
  };

  void  Pack( std::function<void(const void*, size_t)>, const mtc::span<const RankerTag>& );
  void  Pack( std::function<void(const void*, size_t)>, const mtc::span<const MarkupTag>&, FieldHandler& );

  void  Pack( mtc::IByteStream*, const mtc::span<const RankerTag>& );
  void  Pack( mtc::IByteStream*, const mtc::span<const MarkupTag>&, FieldHandler& );

  auto  Pack( const mtc::span<const RankerTag>& ) -> std::vector<char>;
  auto  Pack( const mtc::span<const MarkupTag>&, FieldHandler& ) -> std::vector<char>;

  auto  Unpack( RankerTag*, RankerTag*, const char*, const char* ) -> size_t;

  inline  auto  Unpack( RankerTag*  out, size_t  max, const char* src, size_t  len ) -> size_t
    {  return Unpack( out, out + max, src, src + len );  }

  template <size_t N>
  auto  Unpack( RankerTag (&tbeg)[N], const char* src, const char* end ) -> size_t
    {  return Unpack( tbeg, tbeg + N, src, end );  }

  template <size_t N>
  auto  Unpack( RankerTag (&tbeg)[N], const char* src, size_t  len ) -> size_t
    {  return Unpack( tbeg, tbeg + N, src, src + len );  }

  template <size_t N>
  auto  Unpack( RankerTag (&tbeg)[N], const mtc::span<const char>& src ) -> size_t
  {
    return Unpack( tbeg, tbeg + N, src.data(), src.size() );
  }

  auto  Unpack( const mtc::span<const char>& ) -> std::vector<RankerTag>;

  // FormatBox::TLevel inline implementation

  inline
  auto  FormatBox::TLevel::Set( const char* srcptr, unsigned origin, const char* endptr ) -> TLevel&
  {
    srcptr = ::FetchFrom( ::FetchFrom( ::FetchFrom( srcptr,
      format ),
      uLower ),
      uUpper );

    if ( format & 0x01 )  fmtptr = ::FetchFrom( srcptr, length );
      else fmtptr = srcptr + (length = 0);

    uLower += origin;
    uUpper += uLower;

    if ( endptr != nullptr )
      fmtend = endptr;

    return *this;
  }

  // FormatBox inline implementation

  inline FormatBox::FormatBox( const mtc::span<const char>& fmt )
  {
    if ( fmt.begin() != fmt.end() )
      levels[nlevel = 0].Set( fmt.begin(), 0, fmt.end() );
  }

  inline
  auto  FormatBox::Get( unsigned pos ) -> unsigned
  {
    auto  selfmt = unsigned(0);

    while ( nlevel >= 0 )
    {
      auto& curr = levels[nlevel];

      if ( curr.uUpper < pos )
      {
        if ( (curr.fmtptr += curr.length) < curr.fmtend )
          curr.Set( curr.fmtptr, nlevel > 0 ? levels[nlevel - 1].uLower : 0 );
        else --nlevel;
      }
        else
      if ( curr.uLower > pos )
      {
        for ( auto plevel = levels + nlevel - 1; plevel >= levels; --plevel )
          if ( plevel->Covers( pos ) )
            return plevel->format >> 1;
        break;
      }
        else
      {
        selfmt = curr.format >> 1;

        if ( curr.length != 0 )
        {
          levels[++nlevel].Set( curr.fmtptr, curr.uLower, curr.fmtptr + curr.length );
          curr.fmtptr += curr.length;
          curr.length = 0;
        }
          else
        break;
      }
    }

    return selfmt;
  }

  inline
  auto  FormatBox::begin() -> iterator  {  return nlevel >= 0 ? iterator( this ) : iterator();  }

  inline
  auto  FormatBox::end() -> iterator    {  return iterator();  }

  // FormatBox::iterator inline implementation

  inline
  FormatBox::iterator::iterator( FormatBox* ft )
  {
    if ( ft != nullptr && ft->nlevel >= 0 )
      format = (fmtbox = ft)->levels[ft->nlevel];
    else fmtbox = nullptr;
  }

  inline
  FormatBox::iterator::iterator( const iterator& it ):
    format( it.format ),
    fmtbox( it.fmtbox )
  {
  }

  inline
  auto  FormatBox::iterator:: operator = ( const iterator& it ) -> iterator&
  {
    format = it.format;
    fmtbox = it.fmtbox;
    return *this;
  }

  inline
  auto  FormatBox::iterator::operator ++ () -> iterator&
  {
    if ( fmtbox != nullptr && fmtbox->nlevel >= 0 )
    {
      auto& levels = fmtbox->levels;
      auto& nlevel = fmtbox->nlevel;

      while ( fmtbox->nlevel >= 0 )
      {
        auto& ftcurr = levels[nlevel];

        if ( ftcurr.length != 0 )
        {
          levels[++nlevel].Set( ftcurr.fmtptr, ftcurr.uLower, ftcurr.fmtptr + ftcurr.length );
            ftcurr.fmtptr += ftcurr.length;
            ftcurr.length = 0;
          return format = { levels[nlevel].format >> 1, levels[nlevel].uLower,
            levels[nlevel].uUpper, 0 }, *this;
        }
        if ( ftcurr.fmtptr < ftcurr.fmtend )
        {
          ftcurr.Set( ftcurr.fmtptr, nlevel > 0 ? levels[nlevel - 1].uLower : 0 );

          return format = { levels[nlevel].format >> 1, levels[nlevel].uLower,
            levels[nlevel].uUpper, 0 }, *this;
        }
        --nlevel;
      }
      return format = { unsigned(-1), unsigned(-1), unsigned(-1), unsigned(-1) },
        fmtbox = nullptr, *this;
    }
    throw std::logic_error( "invalid operator++() call @" __FILE__ ":" LINE_STRING );
  }

  inline
  auto  FormatBox::iterator::operator ++ ( int ) -> iterator
  {
    auto  res( *this );
      operator ++ ();
    return res;
  }

}}}

# endif   // !__structo_context_pack_format_hpp__
