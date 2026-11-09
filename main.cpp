// ImageCropper - crop fixed-size (640 x 640) patches from full-size images
// for training data, using a Qt Widgets GUI.
//
// Controls:
//   Mouse move         : move the crop box
//   Left click         : save the crop (you can crop the same image many times)
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
#include <QStatusBar>
#include <QWidget>

#include <algorithm>
#include <iostream>

namespace {

constexpr int kCropSize = 640;

// Widget that displays an image scaled to fit, draws the crop box under the
// mouse, and reports clicks in image coordinates.
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
        update();
    }

    void addSavedRect(const QRect& r) {
        savedRects_.push_back(r);
        update();
    }

    QRect roi() const { return roi_; }

signals:
    void cropRequested(const QRect& roi);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::darkGray);
        if (image_.isNull()) return;

        const QRectF target = imageTargetRect();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, image_);

        const double s = scale();
        auto toWidget = [&](const QRect& r) {
            return QRectF(target.left() + r.x() * s, target.top() + r.y() * s,
                          r.width() * s, r.height() * s);
        };

        // Crops already saved from this image.
        p.setPen(QPen(QColor(255, 200, 0), 1, Qt::DashLine));
        for (const QRect& r : savedRects_) p.drawRect(toWidget(r));

        // Current crop box.
        p.setPen(QPen(Qt::green, 2));
        p.drawRect(toWidget(roi_));
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        updateRoi(e->position());
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (image_.isNull() || e->button() != Qt::LeftButton) return;
        updateRoi(e->position());
        emit cropRequested(roi_);
    }

private:
    double scale() const {
        if (image_.isNull()) return 1.0;
        return std::min(double(width()) / image_.width(),
                        double(height()) / image_.height());
    }

    QRectF imageTargetRect() const {
        const double s = scale();
        const double w = image_.width() * s;
        const double h = image_.height() * s;
        return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
    }

    // Center the crop box on the mouse, clamped to the image bounds.
    void updateRoi(const QPointF& widgetPos) {
        if (image_.isNull()) return;
        const QRectF target = imageTargetRect();
        const double s = scale();
        const int x = int((widgetPos.x() - target.left()) / s);
        const int y = int((widgetPos.y() - target.top()) / s);

        const int left = std::clamp(x - kCropSize / 2, 0, image_.width() - kCropSize);
        const int top = std::clamp(y - kCropSize / 2, 0, image_.height() - kCropSize);
        roi_ = QRect(left, top, kCropSize, kCropSize);
        update();
    }

    QImage image_;
    QRect roi_;
    QVector<QRect> savedRects_;
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
            new QLabel("Click: Save crop | Space/Enter/N: Next | Backspace/P: Previous | "
                       "Esc: Skip | Q: Quit"));
        connect(canvas_, &CropCanvas::cropRequested, this, &MainWindow::saveCrop);
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
        info_->setText(QString("[%1/%2] %3  (%4x%5)  crops saved: %6")
                           .arg(index_ + 1)
                           .arg(files_.size())
                           .arg(currentName())
                           .arg(current_.width())
                           .arg(current_.height())
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
