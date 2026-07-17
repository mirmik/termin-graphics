#include <termin/render/render_task.hpp>

namespace termin {

RenderTaskExtension::RenderTaskExtension() = default;
RenderTaskExtension::~RenderTaskExtension() = default;

RenderTaskList::RenderTaskList() = default;
RenderTaskList::RenderTaskList(RenderTaskList&&) noexcept = default;
RenderTaskList& RenderTaskList::operator=(RenderTaskList&&) noexcept = default;

void RenderTaskList::reserve(size_t count) {
    tasks_.reserve(count);
}

RenderTask& RenderTaskList::append() {
    tasks_.emplace_back();
    return tasks_.back();
}

RenderTask& RenderTaskList::at(size_t index) {
    return tasks_.at(index);
}

const RenderTask& RenderTaskList::at(size_t index) const {
    return tasks_.at(index);
}

bool RenderTaskList::empty() const {
    return tasks_.empty();
}

size_t RenderTaskList::size() const {
    return tasks_.size();
}

std::vector<RenderTask>::iterator RenderTaskList::begin() {
    return tasks_.begin();
}

std::vector<RenderTask>::iterator RenderTaskList::end() {
    return tasks_.end();
}

std::vector<RenderTask>::const_iterator RenderTaskList::begin() const {
    return tasks_.begin();
}

std::vector<RenderTask>::const_iterator RenderTaskList::end() const {
    return tasks_.end();
}

void RenderTask::set_resources(
    const MaterialPipelineResourceView* material_resources,
    std::span<const RenderItemNamedUniformBinding> named_uniforms,
    std::span<const RenderItemNamedTextureBinding> named_textures)
{
    named_uniforms_.assign(named_uniforms.begin(), named_uniforms.end());
    named_textures_.assign(named_textures.begin(), named_textures.end());
    resources = {};
    resources.material_resources = material_resources;
    resources.named_uniforms = named_uniforms_.data();
    resources.named_uniform_count = static_cast<uint32_t>(named_uniforms_.size());
    resources.named_textures = named_textures_.data();
    resources.named_texture_count = static_cast<uint32_t>(named_textures_.size());
}

} // namespace termin
