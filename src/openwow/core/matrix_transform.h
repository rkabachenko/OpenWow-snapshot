#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openwow::core {

inline constexpr std::uint32_t kMatrixTransform156TotalColumns = 156;
inline constexpr std::uint32_t kMatrixTransform176TotalColumns = 176;
inline constexpr std::uint32_t kMatrixTransformMaxRowInputs = 8;
inline constexpr std::uint32_t kMatrixTransformMaxColumnRows = 6;

struct MatrixTransformBitMatrix {
  std::string_view name;
  std::uint32_t bit_count = 0;
  std::uint32_t stride = 0;
  std::span<const std::uint8_t> data;
  bool borrowed = false;
};

struct RetailMatrixTransformTables {
  std::uint32_t total_columns = 0;
  MatrixTransformBitMatrix source_bits;
  MatrixTransformBitMatrix check_bits;
  MatrixTransformBitMatrix inverse_check_bits;
};

struct MatrixTransformRowLinks {
  std::uint32_t input_count = 0;
  std::array<std::uint16_t, kMatrixTransformMaxRowInputs> input_indices{};
};

struct MatrixTransformColumnLinks {
  std::uint16_t index = 0;
  std::uint8_t row_count = 0;
  std::array<std::uint16_t, kMatrixTransformMaxColumnRows> row_indices{};
};

struct MatrixTransform {
  std::uint32_t total_columns = 0;
  std::uint32_t input_columns = 0;
  std::uint32_t check_columns = 0;
  std::uint32_t row_count = 0;
  MatrixTransformBitMatrix source_bits;
  MatrixTransformBitMatrix check_bits;
  MatrixTransformBitMatrix inverse_check_bits;
  std::vector<MatrixTransformRowLinks> rows;
  std::vector<MatrixTransformColumnLinks> columns;
};

[[nodiscard]] const RetailMatrixTransformTables* GetRetailMatrixTransformTables(
    std::uint32_t total_columns);

[[nodiscard]] bool ReadMatrixBit(const MatrixTransformBitMatrix& matrix,
                                 std::uint32_t row,
                                 std::uint32_t column);

[[nodiscard]] bool CMatrixTransform_BuildSourceBit(MatrixTransform* transform);

}
