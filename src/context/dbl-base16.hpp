# if !defined( STRUCTO_CONTEXT_DOUBLE_BASE16_HPP_ )
# define STRUCTO_CONTEXT_DOUBLE_BASE16_HPP_

namespace structo {
namespace context {

 /*
  * Store - писатель данных по квартетам
  */
  template <size_t length, char chfill>
  class Store
  {
    char  buffer[length];
    char* bufend = buffer;
    bool  bupper = true;


  public: // write
    void store( unsigned value )
    {
      if ( !(bupper = !bupper) ) *bufend = (value << 4) | (chfill & 0x0f);
        else { *bufend = (*bufend & 0xf0) | value; ++bufend; }
    }
    template <class ... Values>
    void store( unsigned value, Values ... items )
    {
      return store( value ), store( items... );
    }

  public:
    auto size() const -> size_t { return bufend - buffer + (bupper ? 0 : 1); }
    auto data() const -> const void* { return buffer; }

  };

  template <size_t length, char chfill>
  struct negative_flush: public Store<length, chfill>
  {
    auto put_mantis( int xpower, int mantis ) -> negative_flush&
    {
      if ( xpower >= 0 ) this->store( 0x3 - ((xpower >> 4) & 0x3), 0xf - (xpower & 0x0f) );
        else this->store( 0x4 | (-xpower >> 4) & 0x03, (-xpower) & 0x0f );
      return put_nibble( mantis );
    }
    auto put_nibble( unsigned u ) -> negative_flush&
    {
      return this->store( 0xf - (u & 0xf) ), *this;
    }
    template <class ... Nibbles>
    auto put_nibble( unsigned u, Nibbles... n ) -> negative_flush&
    {
      return put_nibble( u ).put_nibble( n... );
    }
  };

  template <size_t length, char chfill>
  struct positive_flush: public Store<length, chfill>
  {
    auto put_mantis( int xpower, int mantis ) -> positive_flush&
    {
      if ( xpower >= 0 ) this->store( 0x8 | (mantis != 0 ? 0x4 : 0) | ((xpower >> 4) & 0x3), xpower & 0x0f );
        else this->store( 0x8 | (0x3 - ((-xpower >> 4) & 0x3)), (0xf - (-xpower) & 0x0f) );

      return this->store( mantis ), *this;
    }
    auto put_nibble( unsigned u ) -> positive_flush&
    {
      return this->store( u ), *this;
    }
    template <class ... Nibbles>
    auto put_nibble( unsigned u, Nibbles... n ) -> positive_flush&
    {
      return put_nibble( u ).put_nibble( n... );
    }
  };

  template <class Store, class Value>
  static auto make_double_key( Store& flush, const Value& value ) -> const Store&
  {
    auto divide = value;
    auto mantis = (int)value;
    auto xpower = (int)0;

    static_assert( std::is_floating_point<Value>::value,
      "make_double_key( store, value ) would be called for floating point values!" );

  // убедиться в корректности вызова функции - только положительные
    assert( value >= 0.0 );

  // отсеять 0.0
  // выделить целую часть и степень
    if ( value >= 1.0 ) while ( divide >= 16.0 ) { mantis = (divide /= 16.0); ++xpower; }
      else
    if ( value != 0.0 ) while ( divide < 1.0 ) { mantis = (divide *= 16.0); --xpower; }
      else
    return flush.put_mantis( 0, 0 );

    flush.put_mantis( xpower, mantis );

    if ( mantis != 0 )
    {
      auto  dleast = fmod( value, mantis * pow( 16, xpower ) );
      auto  dpower = pow( 16, xpower - 1 );

      for ( ; dleast != 0.0; dpower /= 16.0 )
      {
        auto  ndigit = (int)(dleast / dpower);

        flush.put_nibble( ndigit );
        dleast -= ndigit * dpower;
      }
    }

    return flush;
  }

}}

# endif   // !STRUCTO_CONTEXT_DOUBLE_BASE16_HPP_
