// ImageProcessingPlugin.cpp
#ifdef HAS_OPENCV

#include "ImageProcessingPlugin.h"
#include <opencv2/imgproc.hpp>
#include <iostream>

ImageProcessingPlugin::ImageProcessingPlugin()
    : VisionPluginBase("ImageProcessing", "Filter", "Basic image processing: rotate, flip, grayscale, binary, gaussian blur")
{
}

bool ImageProcessingPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
{
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    std::string operationName;

    try {
        switch (m_operation) {
        case Operation::None:
            output = input.clone();
            operationName = "None";
            break;

        case Operation::RotateLeft:
            // Rotate 90 degrees counter-clockwise
            cv::rotate(input, output, cv::ROTATE_90_COUNTERCLOCKWISE);
            operationName = "Rotate Left (90 CCW)";
            break;

        case Operation::RotateRight:
            // Rotate 90 degrees clockwise
            cv::rotate(input, output, cv::ROTATE_90_CLOCKWISE);
            operationName = "Rotate Right (90 CW)";
            break;

        case Operation::Rotate180:
            // Rotate 180 degrees
            cv::rotate(input, output, cv::ROTATE_180);
            operationName = "Rotate 180";
            break;

        case Operation::FlipHorizontal:
            // Flip horizontally (mirror around y-axis)
            cv::flip(input, output, 1);
            operationName = "Flip Horizontal";
            break;

        case Operation::FlipVertical:
            // Flip vertically (mirror around x-axis)
            cv::flip(input, output, 0);
            operationName = "Flip Vertical";
            break;

        case Operation::Grayscale:
            // Convert to grayscale
            if (input.channels() == 3) {
                cv::Mat gray;
                cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
                // Convert back to BGR for consistent output format
                cv::cvtColor(gray, output, cv::COLOR_GRAY2BGR);
            } else if (input.channels() == 1) {
                cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
            } else {
                output = input.clone();
            }
            operationName = "Grayscale";
            break;

        case Operation::Binary:
            {
                // Convert to grayscale first
                cv::Mat gray;
                if (input.channels() == 3) {
                    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
                } else if (input.channels() == 1) {
                    gray = input;
                } else {
                    m_lastError = "Unsupported number of channels for binary: " + std::to_string(input.channels());
                    return false;
                }

                // Apply threshold
                cv::Mat binary;
                cv::threshold(gray, binary, m_threshold, m_maxValue, m_thresholdType);

                // Convert back to BGR for consistent output format
                cv::cvtColor(binary, output, cv::COLOR_GRAY2BGR);

                // Add threshold-specific measurements
                int whitePixels = cv::countNonZero(binary);
                int totalPixels = binary.rows * binary.cols;
                int blackPixels = totalPixels - whitePixels;
                double whiteRatio = (whitePixels * 100.0) / totalPixels;

                result.addMeasurement("white_pixels", static_cast<double>(whitePixels));
                result.addMeasurement("black_pixels", static_cast<double>(blackPixels));
                result.addMeasurement("white_ratio_percent", whiteRatio);

                operationName = "Binary Threshold";
            }
            break;

        case Operation::GaussianBlur:
            {
                int ksize = m_blurKernelSize;
                if (ksize % 2 == 0) ksize++;  // Ensure odd

                cv::GaussianBlur(input, output, cv::Size(ksize, ksize), m_blurSigmaX, m_blurSigmaY);
                operationName = "Gaussian Blur";
            }
            break;

        default:
            output = input.clone();
            operationName = "Unknown";
            break;
        }
    }
    catch (const cv::Exception& e) {
        m_lastError = "OpenCV error: " + std::string(e.what());
        return false;
    }

    // Add standard measurements
    result.addMeasurement("input_width", static_cast<double>(input.cols));
    result.addMeasurement("input_height", static_cast<double>(input.rows));
    result.addMeasurement("input_channels", static_cast<double>(input.channels()));
    result.addMeasurement("output_width", static_cast<double>(output.cols));
    result.addMeasurement("output_height", static_cast<double>(output.rows));
    result.addMeasurement("output_channels", static_cast<double>(output.channels()));

    // Add metadata
    result.addMetadata("operation", operationName);
    result.addMetadata("plugin", "ImageProcessing");

    std::cout << "[ImageProcessing] Applied: " << operationName
              << " (" << input.cols << "x" << input.rows
              << " -> " << output.cols << "x" << output.rows << ")" << std::endl;

    return true;
}

bool ImageProcessingPlugin::validate() const
{
    if (m_operation == Operation::Binary) {
        if (m_threshold < 0 || m_threshold > 255) {
            m_lastError = "Threshold must be between 0 and 255";
            return false;
        }
        if (m_maxValue < 0 || m_maxValue > 255) {
            m_lastError = "Max value must be between 0 and 255";
            return false;
        }
        if (m_thresholdType < 0 || m_thresholdType > 4) {
            m_lastError = "Invalid threshold type";
            return false;
        }
    }

    if (m_operation == Operation::GaussianBlur) {
        if (m_blurKernelSize < 1) {
            m_lastError = "Blur kernel size must be at least 1";
            return false;
        }
        if (m_blurKernelSize % 2 == 0) {
            m_lastError = "Blur kernel size must be odd";
            return false;
        }
        if (m_blurSigmaX < 0) {
            m_lastError = "Sigma X cannot be negative";
            return false;
        }
        if (m_blurSigmaY < 0) {
            m_lastError = "Sigma Y cannot be negative";
            return false;
        }
    }

    return true;
}

QWidget* ImageProcessingPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Operation selection
    QGroupBox* operationGroup = new QGroupBox("Operation");
    QFormLayout* operationLayout = new QFormLayout(operationGroup);

    m_operationCombo = new QComboBox();
    m_operationCombo->addItem("None", static_cast<int>(Operation::None));
    m_operationCombo->addItem("Rotate Left (90 CCW)", static_cast<int>(Operation::RotateLeft));
    m_operationCombo->addItem("Rotate Right (90 CW)", static_cast<int>(Operation::RotateRight));
    m_operationCombo->addItem("Rotate 180", static_cast<int>(Operation::Rotate180));
    m_operationCombo->addItem("Flip Horizontal", static_cast<int>(Operation::FlipHorizontal));
    m_operationCombo->addItem("Flip Vertical", static_cast<int>(Operation::FlipVertical));
    m_operationCombo->addItem("Grayscale", static_cast<int>(Operation::Grayscale));
    m_operationCombo->addItem("Binary Threshold", static_cast<int>(Operation::Binary));
    m_operationCombo->addItem("Gaussian Blur", static_cast<int>(Operation::GaussianBlur));
    m_operationCombo->setCurrentIndex(static_cast<int>(m_operation));
    m_operationCombo->setToolTip("Select the image processing operation to apply");
    operationLayout->addRow("Operation:", m_operationCombo);

    mainLayout->addWidget(operationGroup);

    // Binary threshold parameters
    m_thresholdGroup = new QGroupBox("Binary Threshold Parameters");
    QFormLayout* thresholdLayout = new QFormLayout(m_thresholdGroup);

    m_thresholdSpin = new QSpinBox();
    m_thresholdSpin->setRange(0, 255);
    m_thresholdSpin->setValue(m_threshold);
    m_thresholdSpin->setToolTip("Threshold value (0-255). Pixels above this become white.");
    thresholdLayout->addRow("Threshold:", m_thresholdSpin);

    m_maxValueSpin = new QSpinBox();
    m_maxValueSpin->setRange(0, 255);
    m_maxValueSpin->setValue(m_maxValue);
    m_maxValueSpin->setToolTip("Maximum value assigned to pixels above threshold");
    thresholdLayout->addRow("Max Value:", m_maxValueSpin);

    m_thresholdTypeCombo = new QComboBox();
    m_thresholdTypeCombo->addItem("Binary", 0);
    m_thresholdTypeCombo->addItem("Binary Inverted", 1);
    m_thresholdTypeCombo->addItem("Truncate", 2);
    m_thresholdTypeCombo->addItem("To Zero", 3);
    m_thresholdTypeCombo->addItem("To Zero Inverted", 4);
    m_thresholdTypeCombo->setCurrentIndex(m_thresholdType);
    m_thresholdTypeCombo->setToolTip("Type of thresholding operation");
    thresholdLayout->addRow("Type:", m_thresholdTypeCombo);

    mainLayout->addWidget(m_thresholdGroup);

    // Gaussian blur parameters
    m_blurGroup = new QGroupBox("Gaussian Blur Parameters");
    QFormLayout* blurLayout = new QFormLayout(m_blurGroup);

    m_blurKernelSizeSpin = new QSpinBox();
    m_blurKernelSizeSpin->setRange(1, 99);
    m_blurKernelSizeSpin->setSingleStep(2);
    m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    m_blurKernelSizeSpin->setToolTip("Kernel size (must be odd). Larger = more blur.");
    blurLayout->addRow("Kernel Size:", m_blurKernelSizeSpin);

    m_blurSigmaXSpin = new QDoubleSpinBox();
    m_blurSigmaXSpin->setRange(0.0, 100.0);
    m_blurSigmaXSpin->setSingleStep(0.5);
    m_blurSigmaXSpin->setValue(m_blurSigmaX);
    m_blurSigmaXSpin->setToolTip("Standard deviation in X direction. 0 = auto calculate from kernel size.");
    blurLayout->addRow("Sigma X:", m_blurSigmaXSpin);

    m_blurSigmaYSpin = new QDoubleSpinBox();
    m_blurSigmaYSpin->setRange(0.0, 100.0);
    m_blurSigmaYSpin->setSingleStep(0.5);
    m_blurSigmaYSpin->setValue(m_blurSigmaY);
    m_blurSigmaYSpin->setToolTip("Standard deviation in Y direction. 0 = same as Sigma X.");
    blurLayout->addRow("Sigma Y:", m_blurSigmaYSpin);

    mainLayout->addWidget(m_blurGroup);
    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_operationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                m_operation = static_cast<Operation>(m_operationCombo->itemData(index).toInt());
                updateParameterVisibility();
            });

    QObject::connect(m_thresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_threshold = val; });
    QObject::connect(m_maxValueSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxValue = val; });
    QObject::connect(m_thresholdTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) { m_thresholdType = index; });

    QObject::connect(m_blurKernelSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) {
                if (val % 2 == 0) val++;
                m_blurKernelSize = val;
            });
    QObject::connect(m_blurSigmaXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_blurSigmaX = val; });
    QObject::connect(m_blurSigmaYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_blurSigmaY = val; });

    // Initial visibility
    updateParameterVisibility();

    return m_configWidget;
}

void ImageProcessingPlugin::updateParameterVisibility()
{
    if (m_thresholdGroup) {
        m_thresholdGroup->setVisible(m_operation == Operation::Binary);
    }
    if (m_blurGroup) {
        m_blurGroup->setVisible(m_operation == Operation::GaussianBlur);
    }
}

nlohmann::json ImageProcessingPlugin::getParameters() const
{
    nlohmann::json params;
    params["operation"] = static_cast<int>(m_operation);
    params["threshold"] = m_threshold;
    params["maxValue"] = m_maxValue;
    params["thresholdType"] = m_thresholdType;
    params["blurKernelSize"] = m_blurKernelSize;
    params["blurSigmaX"] = m_blurSigmaX;
    params["blurSigmaY"] = m_blurSigmaY;
    return params;
}

void ImageProcessingPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("operation")) m_operation = static_cast<Operation>(params["operation"].get<int>());
    if (params.contains("threshold")) m_threshold = params["threshold"].get<int>();
    if (params.contains("maxValue")) m_maxValue = params["maxValue"].get<int>();
    if (params.contains("thresholdType")) m_thresholdType = params["thresholdType"].get<int>();
    if (params.contains("blurKernelSize")) m_blurKernelSize = params["blurKernelSize"].get<int>();
    if (params.contains("blurSigmaX")) m_blurSigmaX = params["blurSigmaX"].get<double>();
    if (params.contains("blurSigmaY")) m_blurSigmaY = params["blurSigmaY"].get<double>();

    // Update UI if widgets exist
    if (m_operationCombo) {
        for (int i = 0; i < m_operationCombo->count(); ++i) {
            if (m_operationCombo->itemData(i).toInt() == static_cast<int>(m_operation)) {
                m_operationCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_thresholdSpin) m_thresholdSpin->setValue(m_threshold);
    if (m_maxValueSpin) m_maxValueSpin->setValue(m_maxValue);
    if (m_thresholdTypeCombo) m_thresholdTypeCombo->setCurrentIndex(m_thresholdType);
    if (m_blurKernelSizeSpin) m_blurKernelSizeSpin->setValue(m_blurKernelSize);
    if (m_blurSigmaXSpin) m_blurSigmaXSpin->setValue(m_blurSigmaX);
    if (m_blurSigmaYSpin) m_blurSigmaYSpin->setValue(m_blurSigmaY);

    updateParameterVisibility();
}

nlohmann::json ImageProcessingPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["operation"] = static_cast<int>(Operation::Grayscale);
    params["threshold"] = 128;
    params["maxValue"] = 255;
    params["thresholdType"] = 0;
    params["blurKernelSize"] = 5;
    params["blurSigmaX"] = 0.0;
    params["blurSigmaY"] = 0.0;
    return params;
}

ParameterSchema ImageProcessingPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Operation selection
    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "operation", "Operation",
        {"None", "Rotate Left", "Rotate Right", "Rotate 180", "Flip Horizontal", "Flip Vertical", "Grayscale", "Binary Threshold", "Gaussian Blur"},
        {0, 1, 2, 3, 4, 5, 6, 7, 8},
        6,  // Default: Grayscale
        "Select the image processing operation to apply",
        "Operation"
    ));

    // Binary threshold parameters
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "threshold", "Threshold",
        0, 255, 128,
        "Threshold value. Pixels with intensity above this become white (or as specified by type).",
        "", 1,
        "Binary Threshold"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxValue", "Max Value",
        0, 255, 255,
        "Maximum value assigned to pixels that pass the threshold test.",
        "", 1,
        "Binary Threshold"
    ));

    schema.parameters.push_back(ParameterDescriptor::createChoice(
        "thresholdType", "Threshold Type",
        {"Binary", "Binary Inverted", "Truncate", "To Zero", "To Zero Inverted"},
        {0, 1, 2, 3, 4},
        0,
        "Type of thresholding operation to apply",
        "Binary Threshold"
    ));

    // Gaussian blur parameters
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "blurKernelSize", "Kernel Size",
        1, 99, 5,
        "Gaussian kernel size (must be odd). Larger values produce more blur.",
        "px", 2,
        "Gaussian Blur"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "blurSigmaX", "Sigma X",
        0.0, 100.0, 0.0,
        "Standard deviation in X direction. Set to 0 to auto-calculate from kernel size.",
        "", 0.5,
        "Gaussian Blur"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "blurSigmaY", "Sigma Y",
        0.0, 100.0, 0.0,
        "Standard deviation in Y direction. Set to 0 to use same value as Sigma X.",
        "", 0.5,
        "Gaussian Blur"
    ));

    return schema;
}

#endif // HAS_OPENCV
