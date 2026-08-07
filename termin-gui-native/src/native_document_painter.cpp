#include <termin/gui_native/native_document_painter.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include <tcbase/tc_log.h>
#include <termin/gui_native/color_picker.hpp>
#include <termin/gui_native/draw_list_renderer.hpp>
#include <tgfx2/render_context.hpp>

namespace termin::gui_native {
    namespace {

        struct DrawListDeleter {
            void operator()(tc_ui_draw_list* draw_list) const {
                tc_ui_draw_list_destroy(draw_list);
            }
        };

        struct PaintContextDeleter {
            void operator()(tc_ui_paint_context* context) const {
                tc_ui_paint_context_destroy(context);
            }
        };

        [[noreturn]] void painter_error(const std::string& message) {
            tc_log_error("[gui-native-document-painter] %s", message.c_str());
            throw std::logic_error(message);
        }

        bool contains_document(std::span<const UiDocumentSubmission> submissions, tc_ui_document_handle document) {
            return std::any_of(
                submissions.begin(), submissions.end(), [document](const UiDocumentSubmission& submission) {
                    return tc_ui_document_handle_eq(submission.document.handle(), document);
                });
        }

    } // namespace

    struct NativeDocumentPainter::Impl {
        UiDrawListRenderer renderer;
        std::unique_ptr<tc_ui_draw_list, DrawListDeleter> draw_list{tc_ui_draw_list_create()};
        std::unique_ptr<tc_ui_paint_context, PaintContextDeleter> paint_context{
            tc_ui_paint_context_create(draw_list.get())};
        std::vector<tc_ui_document_handle> measured_documents;
        bool closed = false;

        Impl() {
            if (!draw_list || !paint_context) {
                painter_error("failed to allocate native UI paint state");
            }
        }

        void require_open(const char* operation) const {
            if (closed) {
                painter_error(std::string("NativeDocumentPainter::") + operation + " called after close");
            }
        }

        void synchronize_text_measurers(std::span<const UiDocumentSubmission> submissions) {
            measured_documents.erase(std::remove_if(measured_documents.begin(),
                                                    measured_documents.end(),
                                                    [submissions](tc_ui_document_handle document) {
                                                        if (!tc_ui_document_is_valid(document)) {
                                                            return true;
                                                        }
                                                        if (contains_document(submissions, document)) {
                                                            return false;
                                                        }
                                                        tc_ui_document_set_text_measurer(document, nullptr, nullptr);
                                                        return true;
                                                    }),
                                     measured_documents.end());

            for (const UiDocumentSubmission& submission : submissions) {
                const tc_ui_document_handle document = submission.document.handle();
                const bool already_bound = std::any_of(
                    measured_documents.begin(), measured_documents.end(), [document](tc_ui_document_handle candidate) {
                        return tc_ui_document_handle_eq(candidate, document);
                    });
                if (!already_bound) {
                    renderer.bind_text_measurer(document);
                    measured_documents.push_back(document);
                }
            }
        }

        void close() {
            if (closed) {
                return;
            }
            for (tc_ui_document_handle document : measured_documents) {
                if (tc_ui_document_is_valid(document)) {
                    tc_ui_document_set_text_measurer(document, nullptr, nullptr);
                }
            }
            measured_documents.clear();
            renderer.release_gpu();
            closed = true;
        }
    };

    NativeDocumentPainter::NativeDocumentPainter()
        : impl_(std::make_unique<Impl>()) {}

    NativeDocumentPainter::NativeDocumentPainter(NativeDocumentPainterConfig config)
        : NativeDocumentPainter() {
        if (!config.font_path.empty() && !set_default_font_path(config.font_path, config.font_size)) {
            painter_error("failed to load UI font: " + config.font_path);
        }
    }

    NativeDocumentPainter::~NativeDocumentPainter() {
        if (!impl_ || impl_->closed) {
            return;
        }
        try {
            impl_->close();
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-document-painter] destructor shutdown failed: %s", error.what());
        } catch (...) {
            tc_log_error("[gui-native-document-painter] destructor shutdown failed with unknown exception");
        }
    }

    bool NativeDocumentPainter::set_default_font_path(const std::string& path, int default_size_px) {
        impl_->require_open("set_default_font_path");
        if (path.empty() || default_size_px <= 0) {
            tc_log_error("[gui-native-document-painter] invalid font configuration");
            return false;
        }
        return impl_->renderer.set_default_font_path(path, default_size_px);
    }

    std::size_t NativeDocumentPainter::paint_documents(tgfx::RenderContext2& context,
                                                       int width,
                                                       int height,
                                                       std::span<const UiDocumentSubmission> documents) {
        impl_->require_open("paint_documents");
        if (width <= 0 || height <= 0) {
            painter_error("cannot paint into invalid viewport " + std::to_string(width) + "x" + std::to_string(height));
        }

        std::vector<UiDocumentSubmission> ordered;
        ordered.reserve(documents.size());
        for (const UiDocumentSubmission& submission : documents) {
            if (!submission.document.valid()) {
                tc_log_error("[gui-native-document-painter] skipping invalid document "
                             "identity=%llu priority=%d",
                             static_cast<unsigned long long>(submission.stable_identity),
                             submission.priority);
                continue;
            }
            if (!tc_ui_presentation_metrics_is_valid(&submission.presentation_metrics)) {
                tc_log_error("[gui-native-document-painter] skipping document with invalid "
                             "presentation metrics identity=%llu priority=%d",
                             static_cast<unsigned long long>(submission.stable_identity),
                             submission.priority);
                continue;
            }
            if (submission.presentation_metrics.physical_extent.width != static_cast<float>(width) ||
                submission.presentation_metrics.physical_extent.height != static_cast<float>(height)) {
                tc_log_error("[gui-native-document-painter] skipping document whose "
                             "physical extent %.3fx%.3f does not match render viewport "
                             "%dx%d identity=%llu",
                             submission.presentation_metrics.physical_extent.width,
                             submission.presentation_metrics.physical_extent.height,
                             width,
                             height,
                             static_cast<unsigned long long>(submission.stable_identity));
                continue;
            }
            ordered.push_back(submission);
        }
        std::sort(
            ordered.begin(), ordered.end(), [](const UiDocumentSubmission& left, const UiDocumentSubmission& right) {
                if (left.priority != right.priority) {
                    return left.priority < right.priority;
                }
                if (left.stable_identity != right.stable_identity) {
                    return left.stable_identity < right.stable_identity;
                }
                const tc_ui_document_handle left_handle = left.document.handle();
                const tc_ui_document_handle right_handle = right.document.handle();
                if (left_handle.index != right_handle.index) {
                    return left_handle.index < right_handle.index;
                }
                return left_handle.generation < right_handle.generation;
            });

        impl_->synchronize_text_measurers(ordered);
        tc_ui_draw_list_clear(impl_->draw_list.get());
        std::vector<UiDrawListBatch> batches;
        batches.reserve(ordered.size());
        for (const UiDocumentSubmission& submission : ordered) {
            if (!submission.document.set_presentation_metrics(submission.presentation_metrics)) {
                tc_log_error("[gui-native-document-painter] document rejected validated "
                             "presentation metrics identity=%llu",
                             static_cast<unsigned long long>(submission.stable_identity));
                continue;
            }
            tc_ui_rect layout_rect{};
            if (!submission.document.presentation_layout_rect(layout_rect)) {
                tc_log_error("[gui-native-document-painter] document has no logical layout "
                             "rect identity=%llu",
                             static_cast<unsigned long long>(submission.stable_identity));
                continue;
            }
            const std::size_t first = tc_ui_draw_list_command_count(impl_->draw_list.get());
            submission.document.layout_roots(layout_rect);
            submission.document.paint(impl_->paint_context.get());
            const std::size_t last = tc_ui_draw_list_command_count(impl_->draw_list.get());
            batches.push_back(UiDrawListBatch{
                first,
                last - first,
                submission.presentation_metrics,
            });
        }
        impl_->renderer.render(context, impl_->draw_list.get(), width, height, batches);
        return batches.size();
    }

    void NativeDocumentPainter::sync_color_picker_surfaces(tgfx::RenderContext2& context, ColorPicker& picker) {
        impl_->require_open("sync_color_picker_surfaces");
        impl_->renderer.sync_color_picker_surfaces(context, picker);
    }

    void NativeDocumentPainter::release_color_picker_surfaces(ColorPicker& picker) {
        impl_->require_open("release_color_picker_surfaces");
        impl_->renderer.release_color_picker_surfaces(picker);
    }

    void NativeDocumentPainter::release_gpu() {
        impl_->require_open("release_gpu");
        impl_->renderer.release_gpu();
    }

    void NativeDocumentPainter::close() {
        if (impl_) {
            impl_->close();
        }
    }

    bool NativeDocumentPainter::is_open() const {
        return impl_ && !impl_->closed;
    }

} // namespace termin::gui_native
