# include "../../context/fields-man.hpp"

#include <compat.hpp>
# include <mtc/wcsstr.h>
# include <unordered_map>
# include <shared_mutex>
#include <mtc/recursive_shared_mutex.hpp>

namespace structo {
namespace context {

  bool  operator == ( const mtc::zmap::key& k, const char* s )
  {
    if ( !k.is_charstr() )
      throw std::invalid_argument( "keys must be strings" );
    return strcmp( k.to_charstr(), s ) == 0;
  }

  bool  operator != ( const mtc::zmap::key& k, const char* s )
  {
    return !(k == s);
  }

  struct OptionsValue: FieldOptions
  {
    std::string namePlace;

    OptionsValue( unsigned fdId, const std::string& fdSz ): namePlace( fdSz )
    {
      id = fdId;
      name = namePlace;
    }

  };

  struct FieldManager::impl
  {
    using str_map_t = std::unordered_map<std::string, std::shared_ptr<OptionsValue>>;
    using int_map_t = std::unordered_map<unsigned,    std::shared_ptr<OptionsValue>>;

    std::shared_mutex fmutex;
    str_map_t         strmap;
    int_map_t         intmap;
  };

  auto  FieldManager::Add( const std::string_view& name ) -> FieldOptions*
  {
    impl::str_map_t::iterator  pfound;

    if ( data == nullptr )
      data = std::make_shared<impl>();

    auto  shlock = mtc::make_shared_lock( data->fmutex );
    auto  exlock = mtc::make_unique_lock( data->fmutex, std::defer_lock );

    if ( (pfound = data->strmap.find( std::string( name ) )) == data->strmap.end() )
    {
      shlock.unlock();  exlock.lock();

      if ( (pfound = data->strmap.find( std::string( name ) )) == data->strmap.end() )
      {
        auto  nextId = uint32_t(data->strmap.size());
        auto  fdName = std::string( name );
        auto  pfield = std::make_shared<OptionsValue>( nextId, fdName );

        pfound =
          data->strmap.insert( { fdName,
          data->intmap.insert( { nextId, pfield } ).first->second} ).first;
      }
    }

    return pfound->second.get();
  }

  auto  FieldManager::Get( const std::string_view& name ) const -> const FieldOptions*
  {
    if ( data != nullptr )
    {
      auto  shlock = mtc::make_shared_lock( data->fmutex );
      auto  pfound = data->strmap.find( std::string( name ) );

      if ( pfound != data->strmap.end() )
        return pfound->second.get();
    }
    return nullptr;
  }

  auto  FieldManager::Get( unsigned id ) const -> const FieldOptions*
  {
    if ( data != nullptr )
    {
      auto  shlock = mtc::make_shared_lock( data->fmutex );
      auto  pfound = data->intmap.find( id );

      if ( pfound != data->intmap.end() )
        return pfound->second.get();
    }
    return nullptr;
  }

// load/save fields

  auto  LoadFields( const mtc::zmap& cfg, const mtc::zmap::key& key ) -> FieldManager
  {
    auto  pval = cfg.get( key );

    if ( pval == nullptr )
      return {};

    switch ( pval->get_type() )
    {
      case mtc::zval::z_array_zmap:
        return LoadFields( *pval->get_array_zmap() );

      case mtc::zval::z_array_zval:
        if ( !pval->get_array_zval()->empty() )
          throw std::invalid_argument( "fields list is expected to be array of structures" );
        return {};

      default:
        throw std::invalid_argument( "fields list is expected to be array of structures" );
    }
  }

  template <class U, class I>
  void  GetValue( U& u, I i, const char* n )
  {
    if ( double(i) > std::numeric_limits<U>::max() || double(i) < std::numeric_limits<U>::min() )
      throw std::invalid_argument( mtc::strprintf( "field '%s' value out of range", n ) );
    u = (U)i;
  }

  void  GetValue( uint32_t& v, const mtc::zval& z, const char* n )
  {
    switch ( z.get_type() )
    {
      case mtc::zval::z_word16:   return GetValue( v, *z.get_word16(), n );
      case mtc::zval::z_word32:   return GetValue( v, *z.get_word32(), n );
      case mtc::zval::z_word64:   return GetValue( v, *z.get_word64(), n );
      case mtc::zval::z_int16:    return GetValue( v, *z.get_int16(), n );
      case mtc::zval::z_int32:    return GetValue( v, *z.get_int32(), n );
      case mtc::zval::z_int64:    return GetValue( v, *z.get_int64(), n );
      default:
        throw std::invalid_argument( mtc::strprintf( "field '%s' has to be integer", n ) );
    }
  }

  template <class MinMax>
  void  LoadMinMax( MinMax& to, const mtc::zmap& indent )
  {
    for ( auto& next: indent )
    {
      if ( !next.first.is_charstr() )
        throw std::invalid_argument( "field indents may contain only string keys" );

      if ( strcmp( next.first.to_charstr(), "min" ) == 0 )  GetValue( to.min, next.second, "min" );
        else
      if ( strcmp( next.first.to_charstr(), "max" ) == 0 )  GetValue( to.max, next.second, "max" );
        else
      throw std::invalid_argument( "field indents may contain only string keys 'min' and 'max'" );
    }
  }

  void  GetIndents( FieldOptions& opts, const mtc::zmap& indents )
  {
    for ( auto next: indents )
    {
      if ( !next.first.is_charstr() )
        throw std::invalid_argument( "field 'indents' may contain only string keys" );

      if ( next.second.get_type() != mtc::zval::z_zmap )
        throw std::invalid_argument( "field 'indents' has to have struct values" );

      if ( next.first == "l" )  LoadMinMax( opts.indents.lower, *next.second.get_zmap() );
        else
      if ( next.first == "h" )  LoadMinMax( opts.indents.upper, *next.second.get_zmap() );
        else
      throw std::invalid_argument( "field 'indents' has to have only 'l' and 'h' keys" );
    }
  }

 /*
  * options format:
  *   "key: value[; key: value[...]]"
  *
  * keys:           values:
  *   word-break      on, true, yes | off, false, no
  *   contents:       text, keys, int ...
  */
  void  GetOptions( FieldOptions& opts, const std::string& options )
  {
    for ( auto strptr = options.c_str(); *(strptr = mtc::ltrim( strptr )) != 0; )
    {
      const char* keytop;
      const char* keyend;
      const char* valtop;
      const char* valend;

    // select key
      for ( keytop = strptr; *strptr != '\0' && *strptr != ':' && !mtc::isspace( *strptr ); ++strptr )
        (void)NULL;

      if ( *(strptr = mtc::ltrim( keyend = strptr )) != ':' )
      {
        throw std::invalid_argument( mtc::strprintf( "invalid field option, ':' expected after '%s'",
          std::string( keytop, strptr ).c_str() ) );
      }
        else
      strptr = mtc::ltrim( strptr + 1 );

    // select value
      for ( valtop = strptr; *strptr != '\0' && *strptr != ';' && !mtc::isspace( *strptr ); ++strptr )
        (void)NULL;

      if ( *(strptr = mtc::ltrim( valend = strptr )) != '\0' && *strptr != ';' )
        throw std::invalid_argument( "invalid field option, ';' or end of string expected" );

    // check key and value
      auto  keystr = std::string( keytop, keyend );
      auto  valstr = std::string( valtop, valend );

      if ( keystr == "word-break" )
      {
        if ( valstr == "true" || valstr == "on" || valstr == "yes" )  opts.options &= FieldOptions::ofNoBreakWords;
          else
        if ( valstr == "false" || valstr == "off" || valstr == "no" ) opts.options |= FieldOptions::ofNoBreakWords;
          else
        throw std::invalid_argument( mtc::strprintf( "invalid field 'word-break' option '%s'", valstr.c_str() ) );
      }
        else
      if ( keystr == "contents" )
      {
      }
        else
      throw std::invalid_argument( mtc::strprintf( "unknown field option '%s'", keystr.c_str() ) );

    // skip to the next string
      if ( *strptr == ';' )
        strptr = mtc::ltrim( strptr + 1 );
    }
  }

  void  ParseField( FieldOptions& opts, const mtc::zmap& field )
  {
    for ( auto& next: field )
    {
      if ( next.first == "indents" )
      {
        if ( next.second.get_type() == mtc::zval::z_zmap )  GetIndents( opts, *next.second.get_zmap() );
          else throw std::invalid_argument( "field 'indents' has to be structure" );
      }
        else
      if ( next.first == "options" )
      {
        if ( next.second.get_type() == mtc::zval::z_charstr )
        {
          GetOptions( opts, *next.second.get_charstr() );
        }
          else
        if ( next.second.get_type() == mtc::zval::z_word32 )
        {
          opts.options = *next.second.get_word32();
        }
          else
        throw std::invalid_argument( "field 'options' has to be string or uint32" );
      }
        else
      if ( next.first == "id" )
      {
        if ( next.second.cast_to_word32( -1 ) != opts.id )
        {
          throw std::invalid_argument( mtc::strprintf( "invalid field '%s' order, 'id' mismatch @" __FILE__ ":" LINE_STRING,
            field.get_charstr( "name", "???" ).c_str() ) );
        }
      }
        else
      if ( next.first != "name" )
      {
        throw std::invalid_argument( mtc::strprintf( "unexpected field '%s' @" __FILE__ ":" LINE_STRING,
          next.first.to_charstr() ) );
      }
    }
  }

  auto  LoadFields( const mtc::array_zmap& cfg ) -> FieldManager
  {
    FieldManager  fields;

    for ( auto& next: cfg )
    {
      auto  name = next.get_charstr( "name" );

      if ( name == nullptr )
      {
        throw next.get( "name" ) == nullptr ?
          std::invalid_argument( "field description has to have 'name' string field" )
        : std::invalid_argument( "field 'name' has to be string" );
      }

      if ( fields.Get( *name ) != nullptr )
      {
        throw std::invalid_argument(
          "field 'name' already exists" );
      }

      ParseField( *fields.Add( *name ), next );
    }
    return fields;
  }

  auto  SaveFields( const FieldManager& fields ) -> mtc::array_zmap
  {
    mtc::array_zmap serial;

    for ( auto u = 0; ; ++u )
    {
      auto  pfield = fields.Get( u );

      if ( pfield == nullptr )
        break;

      serial.push_back( {
        { "name",     std::string( pfield->name ) },
        { "id",       pfield->id },
        { "options",  pfield->options },
        { "indents",  mtc::zmap{
          { "l", mtc::zmap{
            { "min", pfield->indents.lower.min },
            { "max", pfield->indents.lower.max } } },
          { "h", mtc::zmap{
            { "min", pfield->indents.upper.min },
            { "max", pfield->indents.upper.max } } } } } } );
    }
    return serial;
  }

}}
