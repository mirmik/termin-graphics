#include <termin/render/frame_graph_resource_registry.hpp>

#include <cstring>
#include <string>
#include <unordered_map>

#include <tcbase/tc_log.hpp>

namespace termin {
    namespace {

        struct ResourceTypeRecord {
            FrameGraphResourceCreateFn create = nullptr;
            FrameGraphResourceSampledTextureFn sampled_texture = nullptr;
        };

        using ResourceTypeRegistry = std::unordered_map<std::string, ResourceTypeRecord>;

        ResourceTypeRegistry& resource_type_registry() {
            static ResourceTypeRegistry registry;
            return registry;
        }

        bool valid_resource_type(const char* resource_type) {
            return resource_type && resource_type[0] != '\0';
        }

    } // namespace

    bool register_frame_graph_resource_type(const FrameGraphResourceTypeDescriptor& descriptor) {
        if (!valid_resource_type(descriptor.resource_type) || !descriptor.create) {
            tc::Log::error("[FrameGraphResourceRegistry] cannot register a resource type "
                           "without a non-empty name and create function");
            return false;
        }

        const bool inserted =
            resource_type_registry()
                .emplace(descriptor.resource_type, ResourceTypeRecord{descriptor.create, descriptor.sampled_texture})
                .second;
        if (!inserted) {
            tc::Log::error("[FrameGraphResourceRegistry] resource type '%s' is already registered",
                           descriptor.resource_type);
            return false;
        }
        return true;
    }

    bool unregister_frame_graph_resource_type(const char* resource_type) {
        if (!valid_resource_type(resource_type)) {
            tc::Log::error("[FrameGraphResourceRegistry] cannot unregister an empty resource type");
            return false;
        }
        if (resource_type_registry().erase(resource_type) == 0) {
            tc::Log::error("[FrameGraphResourceRegistry] resource type '%s' is not registered", resource_type);
            return false;
        }
        return true;
    }

    bool has_frame_graph_resource_type(const char* resource_type) {
        return valid_resource_type(resource_type) &&
               resource_type_registry().find(resource_type) != resource_type_registry().end();
    }

    bool frame_graph_resource_type_matches(const FrameGraphResourceTypeDescriptor& descriptor) {
        if (!valid_resource_type(descriptor.resource_type)) {
            return false;
        }
        auto it = resource_type_registry().find(descriptor.resource_type);
        return it != resource_type_registry().end() && it->second.create == descriptor.create &&
               it->second.sampled_texture == descriptor.sampled_texture;
    }

    void clear_frame_graph_resource_types() {
        resource_type_registry().clear();
    }

    FrameGraphResource* create_frame_graph_resource(const ResourceSpec& spec) {
        auto it = resource_type_registry().find(spec.resource_type);
        if (it == resource_type_registry().end()) {
            tc::Log::error("[FrameGraphResourceRegistry] resource '%s' has unknown type '%s'",
                           spec.resource.c_str(),
                           spec.resource_type.c_str());
            return nullptr;
        }

        FrameGraphResource* resource = it->second.create(spec);
        if (!resource) {
            tc::Log::error("[FrameGraphResourceRegistry] factory for type '%s' failed to create resource '%s'",
                           spec.resource_type.c_str(),
                           spec.resource.c_str());
            return nullptr;
        }
        const char* actual_type = resource->resource_type();
        if (!actual_type || std::strcmp(actual_type, spec.resource_type.c_str()) != 0) {
            tc::Log::error("[FrameGraphResourceRegistry] factory for type '%s' returned mismatched type '%s'",
                           spec.resource_type.c_str(),
                           actual_type ? actual_type : "<null>");
            delete resource;
            return nullptr;
        }
        return resource;
    }

    FrameGraphResourceSampledTexture frame_graph_resource_sampled_texture(const FrameGraphResource& resource) {
        const char* resource_type = resource.resource_type();
        if (!valid_resource_type(resource_type)) {
            return {};
        }
        auto it = resource_type_registry().find(resource_type);
        if (it == resource_type_registry().end() || !it->second.sampled_texture) {
            return {};
        }
        return it->second.sampled_texture(resource);
    }

} // namespace termin
