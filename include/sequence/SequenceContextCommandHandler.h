// SequenceContextCommandHandler.h
#pragma once

#include "ICommandHandler.h"

/**
 * @brief Handler for direct SequenceContext operations
 *
 * Allows sequences to:
 * - Set/get values in context
 * - Perform calculations (add, subtract, multiply, divide)
 * - Log context values
 * - Check existence of keys
 *
 * Supported methods:
 * - SetValue: Store a value in context
 * - GetValue: Retrieve a value from context (not typically needed in sequences)
 * - LogValue: Log a value from context
 * - Calculate: Perform arithmetic operations
 * - Clear: Clear a value from context
 */
class SequenceContextCommandHandler : public ICommandHandler {
public:
    SequenceContextCommandHandler();
    ~SequenceContextCommandHandler() override = default;

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context
    ) override;

    std::string GetManagerType() const override { return "context"; }
};
