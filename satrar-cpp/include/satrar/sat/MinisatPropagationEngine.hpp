#pragma once

#include "satrar/sat/CnfFormula.hpp"
#include "satrar/sat/DimacsLiteral.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace satrar::sat {

/// In-process MiniSat used for controlled propagation (SatRAR MA), not full miter checks.
/// Implementation is hidden (pimpl) so tests / app code do not include MiniSat headers.
class MinisatPropagationEngine {
public:
    MinisatPropagationEngine();
    ~MinisatPropagationEngine();
    MinisatPropagationEngine(MinisatPropagationEngine&&) noexcept;
    MinisatPropagationEngine& operator=(MinisatPropagationEngine&&) noexcept;

    MinisatPropagationEngine(const MinisatPropagationEngine&) = delete;
    MinisatPropagationEngine& operator=(const MinisatPropagationEngine&) = delete;

    void loadBaseCnf(const CnfFormula& formula);

    void newContextLevel();
    void cancelToContextLevel(std::uint32_t level);
    [[nodiscard]] std::uint32_t contextLevel() const noexcept;

    bool pushAssumption(DimacsLiteral lit);
    bool propagate(std::vector<DimacsLiteral>* outPropagated = nullptr);

    [[nodiscard]] std::optional<bool> value(DimacsLiteral lit);
    [[nodiscard]] std::optional<DimacsLiteral> reasonOf(DimacsLiteral lit);

    /// CTest / bootstrap smoke (implemented in library TU).
    static bool runSmokeSelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace satrar::sat