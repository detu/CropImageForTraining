// ImageCropper - crop fixed-size (640 x 640) patches from full-size images
// for training data, using a Qt Widgets GUI.
//
// Controls:
//   Mouse move         : move the crop box
//   Left click         : save the crop (you can crop the same image many times)
//   Mouse wheel        : zoom in / out around the mouse cursor
//   + / -              : zoom in / out
//   0 or F             : fit the whole image to the window
//   1                  : zoom to 100% (1 image pixel = 1 screen pixel)
//   Right/middle drag  : pan the zoomed image
//   Space / Enter / N  : next image
//   Backspace / P      : previous image
//   Esc                : skip the current image (same as next)
//   Q                  : quit
//
// Output naming (in the output directory):
//   1st crop : crop_<name>.<ext>
//   2nd crop : crop_<name>_1.<ext>
//   3rd crop : crop_<name>_2.<ext>   ... and so on.

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStatusBar>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

constexpr int kCropSize = 640;

// Widget that displays an image (zoomable and pannable), draws the crop box
// under the mouse, and reports clicks in image coordinates.
class CropCanvas : public QWidget {
    Q_OBJECT
public:
    explicit CropCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setMinimumSize(400, 400);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setImage(const QImage& img) {
        image_ = img;
        savedRects_.clear();
        roi_ = QRect(0, 0, kCropSize, kCropSize);
        fitToWindow();
    }

    void addSavedRect(const QRect& r) {
        savedRects_.push_back(r);
        update();
    }

    QRect roi() const { return roi_; }
    double zoom() const { return scale_; }

    // Scale the whole image to fit the widget (the default view).
    void fitToWindow() {
        fitMode_ = true;
        if (!image_.isNull()) {
            scale_ = fitScale();
            clampOffset();
        }
        update();
        emit zoomChanged(scale_);
    }

    // Multiply the zoom by `factor`, keeping the image point under `anchor`
    // (widget coordinates) fixed on screen.
    void zoomBy(double factor, const QPointF& anchor) {
        if (image_.isNull()) return;
        const double minScale = std::min(fitScale(), 1.0) * 0.5;
        const double newScale = std::clamp(scale_ * factor, minScale, kMaxZoom);
        if (newScale == scale_) return;
        const QPointF imgPt = (anchor - offset_) / scale_;
        scale_ = newScale;
        offset_ = anchor - imgPt * scale_;
        fitMode_ = false;
        clampOffset();
        updateRoi(mapFromGlobal(QCursor::pos()));
        emit zoomChanged(scale_);
    }

    void zoomTo(double newScale) { zoomBy(newScale / scale_, rect().center()); }
    void zoomIn() { zoomBy(kZoomStep, rect().center()); }
    void zoomOut() { zoomBy(1.0 / kZoomStep, rect().center()); }

signals:
    void cropRequested(const QRect& roi);
    void zoomChanged(double scale);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::darkGray);
        if (image_.isNull()) return;

        // Draw only the visible part of the image (fast at high zoom).
        const QRectF imgRect(offset_, QSizeF(image_.width() * scale_, image_.height() * scale_));
        const QRectF visible = imgRect.intersected(QRectF(rect()));
        if (!visible.isEmpty()) {
            const QRectF src((visible.topLeft() - offset_) / scale_, visible.size() / scale_);
            // Smooth when shrinking; show sharp pixels when zoomed in a lot.
            p.setRenderHint(QPainter::SmoothPixmapTransform, scale_ < 2.0);
            p.drawImage(visible, image_, src);
        }

        auto toWidget = [&](const QRect& r) {
            return QRectF(offset_ + QPointF(r.topLeft()) * scale_, QSizeF(r.size()) * scale_);
        };

        // Crops already saved from this image.
        p.setPen(QPen(QColor(255, 200, 0), 1, Qt::DashLine));
        for (const QRect& r : savedRects_) p.drawRect(toWidget(r));

        // Current crop box.
        p.setPen(QPen(Qt::green, 2));
        p.drawRect(toWidget(roi_));
    }

    void resizeEvent(QResizeEvent*) override {
        if (image_.isNull()) return;
        if (fitMode_) {
            fitToWindow();
        } else {
            clampOffset();
        }
    }

    void wheelEvent(QWheelEvent* e) override {
        const double steps = e->angleDelta().y() / 120.0;
        if (steps != 0) zoomBy(std::pow(kZoomStep, steps), e->position());
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (panning_) {
            offset_ += e->position() - panStart_;
            panStart_ = e->position();
            clampOffset();
            update();
            return;
        }
        updateRoi(e->position());
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (image_.isNull()) return;
        if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
            panning_ = true;
            panStart_ = e->position();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        updateRoi(e->position());
        emit cropRequested(roi_);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (panning_ && (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton)) {
            panning_ = false;
            unsetCursor();
            updateRoi(e->position());
        }
    }

    void keyPressEvent(QKeyEvent* e) override {
        switch (e->key()) {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomIn();
            break;
        case Qt::Key_Minus:
            zoomOut();
            break;
        case Qt::Key_0:
        case Qt::Key_F:
            fitToWindow();
            break;
        case Qt::Key_1:
            zoomTo(1.0);
            break;
        default:
            QWidget::keyPressEvent(e);  // let the main window handle it
        }
    }

private:
    static constexpr double kZoomStep = 1.25;
    static constexpr double kMaxZoom = 16.0;

    double fitScale() const {
        return std::min(double(width()) / image_.width(), double(height()) / image_.height());
    }

    // Center the image along an axis where it is smaller than the widget;
    // otherwise keep it from being panned past its edges.
    void clampOffset() {
        const double w = image_.width() * scale_;
        const double h = image_.height() * scale_;
        offset_.setX(w <= width() ? (width() - w) / 2.0 : std::clamp(offset_.x(), width() - w, 0.0));
        offset_.setY(h <= height() ? (height() - h) / 2.0
                                   : std::clamp(offset_.y(), height() - h, 0.0));
    }

    // Center the crop box on the mouse, clamped to the image bounds.
    void updateRoi(const QPointF& widgetPos) {
        if (image_.isNull()) return;
        const QPointF imgPt = (widgetPos - offset_) / scale_;
        const int left = std::clamp(int(imgPt.x()) - kCropSize / 2, 0, image_.width() - kCropSize);
        const int top = std::clamp(int(imgPt.y()) - kCropSize / 2, 0, image_.height() - kCropSize);
        roi_ = QRect(left, top, kCropSize, kCropSize);
        update();
    }

    QImage image_;
    QRect roi_;
    QVector<QRect> savedRects_;
    double scale_ = 1.0;   // widget pixels per image pixel
    QPointF offset_;       // widget position of the image's top-left corner
    bool fitMode_ = true;  // re-fit on resize until the user zooms
    bool panning_ = false;
    QPointF panStart_;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QStringList files, QString outputDir)
        : files_(std::move(files)), outputDir_(std::move(outputDir)) {
        canvas_ = new CropCanvas(this);
        setCentralWidget(canvas_);
        info_ = new QLabel(this);
        statusBar()->addWidget(info_, 1);
        statusBar()->addPermanentWidget(
            new QLabel("Click: Save | Wheel/+/-: Zoom | 0: Fit | 1: 100% | Right-drag: Pan | "
                       "Space: Next | Backspace: Prev | Esc: Skip | Q: Quit"));
        connect(canvas_, &CropCanvas::cropRequested, this, &MainWindow::saveCrop);
        connect(canvas_, &CropCanvas::zoomChanged, this, [this] { updateInfo(); });
        resize(1280, 900);
        loadIndex(0, +1);
    }

protected:
    void keyPressEvent(QKeyEvent* e) override {
        switch (e->key()) {
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_N:
            loadIndex(index_ + 1, +1);
            break;
        case Qt::Key_Escape:
            std::cout << "Skipped by user: " << qPrintable(currentName()) << std::endl;
            loadIndex(index_ + 1, +1);
            break;
        case Qt::Key_Backspace:
        case Qt::Key_P:
            loadIndex(index_ - 1, -1);
            break;
        case Qt::Key_Q:
            std::cout << "Quitting..." << std::endl;
            close();
            break;
        default:
            QMainWindow::keyPressEvent(e);
        }
    }

private slots:
    void saveCrop(const QRect& roi) {
        if (current_.isNull()) return;
        const QString path = nextOutputPath(QFileInfo(files_[index_]));
        if (!current_.copy(roi).save(path)) {
            QMessageBox::warning(this, "Save failed", "Could not save " + path);
            return;
        }
        ++cropsThisImage_;
        canvas_->addSavedRect(roi);
        std::cout << "Saved: " << qPrintable(path) << std::endl;
        statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);
        updateInfo();
    }

private:
    QString currentName() const {
        return (index_ >= 0 && index_ < files_.size()) ? QFileInfo(files_[index_]).fileName()
                                                       : QString();
    }

    // First crop -> crop_<base>.<ext>, then crop_<base>_1.<ext>, _2, ...
    // Existing files are never overwritten, so numbering continues across runs
    // and when revisiting an image.
    QString nextOutputPath(const QFileInfo& src) const {
        const QDir out(outputDir_);
        const QString base = "crop_" + src.completeBaseName();
        const QString ext = src.suffix();
        QString candidate = out.filePath(base + "." + ext);
        for (int n = 1; QFileInfo::exists(candidate); ++n)
            candidate = out.filePath(QString("%1_%2.%3").arg(base).arg(n).arg(ext));
        return candidate;
    }

    // Load image at index i, stepping in direction `step` past unreadable or
    // too-small images. Closes the window when past the last image.
    void loadIndex(int i, int step) {
        while (i >= 0 && i < files_.size()) {
            QImageReader reader(files_[i]);
            reader.setAutoTransform(true);
            QImage img = reader.read();
            const QString name = QFileInfo(files_[i]).fileName();
            if (img.isNull()) {
                std::cout << "Could not load: " << qPrintable(name) << std::endl;
            } else if (img.width() < kCropSize || img.height() < kCropSize) {
                std::cout << "Skipped (image too small): " << qPrintable(name) << " ("
                          << img.width() << "x" << img.height() << ")" << std::endl;
            } else {
                index_ = i;
                current_ = img;
                cropsThisImage_ = 0;
                canvas_->setImage(current_);
                std::cout << "\nOpening: " << qPrintable(name) << std::endl;
                updateInfo();
                return;
            }
            i += step;
        }
        if (i >= files_.size()) {
            std::cout << "\nFinished processing!" << std::endl;
            // Queued so it also works when called from the constructor.
            QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection);
        }
        // i < 0: already at the first usable image; stay where we are.
    }

    void updateInfo() {
        info_->setText(QString("[%1/%2] %3  (%4x%5)  zoom: %6%  crops saved: %7")
                           .arg(index_ + 1)
                           .arg(files_.size())
                           .arg(currentName())
                           .arg(current_.width())
                           .arg(current_.height())
                           .arg(qRound(canvas_->zoom() * 100))
                           .arg(cropsThisImage_));
        setWindowTitle("Crop Tool - " + currentName());
    }

    QStringList files_;
    QString outputDir_;
    CropCanvas* canvas_ = nullptr;
    QLabel* info_ = nullptr;
    QImage current_;
    int index_ = -1;
    int cropsThisImage_ = 0;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <input_directory> [output_directory]" << std::endl;
        return -1;
    }

    const QString inputDir = QString::fromLocal8Bit(argv[1]);
    const QString outputDir =
        argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("cropped_images");

    QDir in(inputDir);
    if (!in.exists()) {
        std::cerr << "Error: directory '" << argv[1] << "' does not exist or is not a directory."
                  << std::endl;
        return -1;
    }
    if (!QDir().mkpath(outputDir)) {
        std::cerr << "Error: could not create output directory '" << qPrintable(outputDir) << "'."
                  << std::endl;
        return -1;
    }

    const QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.bmp", "*.tif", "*.tiff", "*.webp"};
    in.setNameFilters(filters);
    in.setFilter(QDir::Files | QDir::Readable);
    in.setSorting(QDir::Name | QDir::IgnoreCase);

    QStringList files;
    for (const QFileInfo& fi : in.entryInfoList()) files << fi.absoluteFilePath();

    std::cout << "Scanning images in: " << qPrintable(inputDir) << std::endl;
    if (files.isEmpty()) {
        std::cout << "No images found." << std::endl;
        return 0;
    }

    MainWindow w(files, outputDir);
    w.show();
    return app.exec();
}

#include "main.moc"
