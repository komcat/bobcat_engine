// LongEventDetector.h
//
// QApplication subclass that times every event dispatch and logs anything
// that takes longer than kThresholdMs. Used to find UI-thread hot spots —
// slow paint events, log-widget redraws, synchronous I/O on signal slots,
// etc. — that cause the camera display fps to drop and chart-overlay
// uiLatency spikes to reach hundreds of ms.
//
// Cost when no event exceeds the threshold: two steady_clock::now() calls
// per Qt event. On Windows that's ~50 ns each — negligible compared to the
// average event dispatch time.
#pragma once

#include <QApplication>
#include <QEvent>
#include <QMetaObject>
#include <QObject>

#include <chrono>
#include <iostream>
#include <string>

#include "diagnostics/DiagVerbose.h"

class LongEventDetector : public QApplication {
public:
    LongEventDetector(int& argc, char** argv) : QApplication(argc, argv) {}

    // Threshold in milliseconds. Events whose dispatch exceeds this are
    // logged to stdout with their type, receiver class, and objectName.
    static constexpr int kThresholdMs = 50;

    bool notify(QObject* receiver, QEvent* event) override {
        // Capture identity BEFORE the dispatch. The event handler is
        // allowed to delete `receiver` synchronously (deleteLater path,
        // explicit `delete` from a slot, etc.), in which case the pointer
        // is invalid by the time notify() returns. Reading className /
        // objectName here is safe; reading after notify() is not.
        const char* className = receiver ? receiver->metaObject()->className() : "(null)";
        const QByteArray objName = receiver ? receiver->objectName().toUtf8() : QByteArray();
        // For unnamed receivers (anonymous QTimers, internal widgets) the
        // parent chain often identifies the owner. Walk up to two hops.
        const char* parentClass = (receiver && receiver->parent())
            ? receiver->parent()->metaObject()->className() : nullptr;
        const QByteArray parentName = (receiver && receiver->parent())
            ? receiver->parent()->objectName().toUtf8() : QByteArray();
        const char* grandparentClass = (receiver && receiver->parent() && receiver->parent()->parent())
            ? receiver->parent()->parent()->metaObject()->className() : nullptr;
        const QEvent::Type eventType = event->type();

        const auto start = std::chrono::steady_clock::now();
        const bool result = QApplication::notify(receiver, event);
        const auto end = std::chrono::steady_clock::now();

        const int64_t durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                       end - start).count();
        if (durationUs >= kThresholdMs * 1000 && diag::VerboseEnabled()) {
            std::cout << "[LongEvent] " << (durationUs / 1000.0) << "ms"
                      << " type=" << static_cast<int>(eventType)
                      << " (" << eventTypeName(eventType) << ")"
                      << " receiver=" << className;
            if (!objName.isEmpty()) {
                std::cout << " name=\"" << objName.constData() << "\"";
            }
            if (parentClass) {
                std::cout << " parent=" << parentClass;
                if (!parentName.isEmpty()) {
                    std::cout << "(\"" << parentName.constData() << "\")";
                }
            }
            if (grandparentClass) {
                std::cout << " gp=" << grandparentClass;
            }
            std::cout << std::endl;
        }
        return result;
    }

private:
    static const char* eventTypeName(QEvent::Type t) {
        switch (t) {
            case QEvent::Timer:                return "Timer";
            case QEvent::Paint:                return "Paint";
            case QEvent::UpdateRequest:        return "UpdateRequest";
            case QEvent::UpdateLater:          return "UpdateLater";
            case QEvent::MouseButtonPress:     return "MouseButtonPress";
            case QEvent::MouseButtonRelease:   return "MouseButtonRelease";
            case QEvent::MouseMove:            return "MouseMove";
            case QEvent::KeyPress:             return "KeyPress";
            case QEvent::KeyRelease:           return "KeyRelease";
            case QEvent::FocusIn:              return "FocusIn";
            case QEvent::FocusOut:             return "FocusOut";
            case QEvent::Enter:                return "Enter";
            case QEvent::Leave:                return "Leave";
            case QEvent::Resize:               return "Resize";
            case QEvent::Show:                 return "Show";
            case QEvent::Hide:                 return "Hide";
            case QEvent::MetaCall:             return "MetaCall";  // queued signal/slot
            case QEvent::DeferredDelete:       return "DeferredDelete";
            case QEvent::ChildAdded:           return "ChildAdded";
            case QEvent::ChildRemoved:         return "ChildRemoved";
            case QEvent::LayoutRequest:        return "LayoutRequest";
            case QEvent::Polish:               return "Polish";
            case QEvent::PolishRequest:        return "PolishRequest";
            case QEvent::Wheel:                return "Wheel";
            case QEvent::ContextMenu:          return "ContextMenu";
            case QEvent::DynamicPropertyChange: return "DynamicPropertyChange";
            default:                           return "Other";
        }
    }
};
