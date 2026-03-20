# if !defined( __structo_src_indexer_override_entities_hxx__ )
# define __structo_src_indexer_override_entities_hxx__
# include "../../contents.hpp"
# include "dynamic-bitmap.hpp"

namespace structo {
namespace indexer {

  struct Override final
  {
    class Entity;
    class Entities;
  };

  class Override::Entity
  {
    mtc::api<const IEntity> entity;

  public:
    Entity( mtc::api<const IEntity> entity ) : entity( entity ) {}

    auto  Index( uint32_t ) -> mtc::api<const IEntity>;
    auto  Extra( const mtc::api<const mtc::IByteBuffer>& ) -> mtc::api<const IEntity>;
    auto  Bundle( const mtc::api<IStorage::IBundleRepo>&, int64_t ) -> mtc::api<const IEntity>;
  };

  class Override::Entities final: public IContentsIndex::IEntities
  {
    mtc::api<IEntities>   entities;
    const Bitmap<>&       suppress;
    mtc::api<const Iface> lifetime;

  public:
    Entities( mtc::api<IEntities>, const Bitmap<>&, const Iface* );
    Entities( const Entities& );

  // overridables
    auto  Find( uint32_t ) -> Reference override;
    auto  Size() const -> uint32_t override;
    auto  Type() const -> uint32_t override;
    auto  Copy() const -> mtc::api<IEntities> override;

    implement_lifetime_control

  };

}}

# endif   // !__structo_src_indexer_override_entities_hxx__
