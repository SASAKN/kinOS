#include "window.hpp"
#include "console.hpp"
#include "font.hpp"

Window::Window(int width, int height) : width_{width}, height_{height} {
    data_.resize(height);
    for (int y = 0; y < height; ++y) {
        data_[y].resize(width);
    }
    ShadowBufferConfig config{};
    config.shadow_buffer = nullptr;
    config.horizontal_resolution = width;
    config.vertical_resolution = height;

    if (auto err = shadow_buffer_.Initialize(config)) {
        console->PutString("failed to initialize shadow buffer");
    }
}

void Window::DrawTo(PixelWriter& writer, Vector2D<int> pos) {
    if (!transparent_color_) {
        shadow_buffer_.CopyToFrameBuffer(pos);
        return;
    }
    const auto tc = transparent_color_.value();
    for (int y = std::max(0, 0 - pos.y); 
         y < std::min(Height(), writer.Height() - pos.y);
         ++y) {
             for (int x = std::max(0, 0 - pos.x);
                  x < std::min(Width(), writer.Width() - pos.x);
                  ++x) {
                      const auto c = At(Vector2D<int>{x, y});
                      if (c != tc) {
                          writer.Write(pos + Vector2D<int>{x, y}, c);
                      }
         }
    }
}

void Window::Move(Vector2D<int> dst_pos, const Rectangle<int>& src) {
    shadow_buffer_.Move(dst_pos, src);
}

void Window::SetTransparentColor(std::optional<PixelColor> c) {
    transparent_color_ = c;
}

Window::WindowWriter* Window::Writer() {
    return &writer_;
}


const PixelColor& Window::At(Vector2D<int> pos) const{
    return data_[pos.y][pos.x];
}

void Window::Write(Vector2D<int> pos, PixelColor c) {
    data_[pos.y][pos.x] = c;
    shadow_buffer_.Writer().Write(pos, c);
}

int Window::Width() const {
    return width_;
}

int Window::Height() const {
    return height_;
}

namespace {
  const int kCloseButtonWidth = 16;
  const int kCloseButtonHeight = 14;
  const char close_button[kCloseButtonHeight][kCloseButtonWidth + 1] = {
    "...............@",
    ".:::::::::::::$@",
    ".:::::::::::::$@",
    ".:::@@::::@@::$@",
    ".::::@@::@@:::$@",
    ".:::::@@@@::::$@",
    ".::::::@@:::::$@",
    ".:::::@@@@::::$@",
    ".::::@@::@@:::$@",
    ".:::@@::::@@::$@",
    ".:::::::::::::$@",
    ".:::::::::::::$@",
    ".$$$$$$$$$$$$$$@",
    "@@@@@@@@@@@@@@@@",
  };
}


void DrawWindow(PixelWriter& writer, const char* title) {
    auto fill_rect = [&writer](Vector2D<int> pos, Vector2D<int> size, uint32_t c) {
        FillRectangle(writer, pos, size, ToColor(c));
    };
    const auto win_w = writer.Width();
    const auto win_h = writer.Height();

    fill_rect({0, 0},         {win_w, 1},             0xc6c6c6);
    fill_rect({1, 1},         {win_w - 2, 1},         0xffffff);
    fill_rect({0, 0},         {1, win_h},             0xc6c6c6);
    fill_rect({1, 1},         {1, win_h - 2},         0xffffff);
    fill_rect({win_w - 2, 1}, {1, win_h - 2},         0x848484);
    fill_rect({win_w - 1, 0}, {1, win_h},             0x000000);
    fill_rect({2, 2},         {win_w - 4, win_h - 4}, 0xc6c6c6);
    fill_rect({3, 3},         {win_w - 6, 18},        0x009b6b);
    fill_rect({1, win_h - 2}, {win_w - 2, 1},         0x848484);
    fill_rect({0, win_h - 1}, {win_w, 1},             0x000000);

    DrawWindowTitle(writer, title, true);
}

void DrawWindowTitle(PixelWriter& writer, const char* title, bool active) {
    const auto win_w = writer.Width();
    uint32_t bgcolor = 0x848484;
    if (active) {
        bgcolor = 0x009b6b;
    }

    FillRectangle(writer, {3, 3}, {win_w - 6, 18}, ToColor(bgcolor));
    WriteString(writer, {24, 4}, title, ToColor(0xffffff));

    for (int y = 0; y < kCloseButtonHeight; ++y) {
        for (int x = 0; x < kCloseButtonWidth; ++x) {
            PixelColor c = ToColor(0xffffff);
            if (close_button[y][x] == '@') {
                c = ToColor(0x000000);
            } else if (close_button[y][x] == '$') {
                c = ToColor(0x848484);
            } else if (close_button[y][x] == ':') {
                c = ToColor(0xc6c6c6);
            }
            writer.Write({win_w - 5 - kCloseButtonWidth + x, 5 + y}, c);
        }
    }
}