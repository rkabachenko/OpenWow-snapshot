
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::data::dbc {
class DbcLoader;
struct GMSurveySurveysEntry;
}

namespace openwow::game {

inline constexpr std::uint32_t kGMSurveyMaxQuestions = 10;

struct GMSurveyQuestionData {
  std::uint8_t rating{0};
  std::string comment;
};

[[nodiscard]] const data::dbc::GMSurveySurveysEntry* ResolveCurrentGMSurvey(
    const data::dbc::DbcLoader& dbc, std::uint8_t locale_index);

[[nodiscard]] std::optional<std::string_view> ResolveCurrentGMSurveyQuestionText(
    const data::dbc::DbcLoader& dbc, std::uint8_t locale_index, int question_index);

[[nodiscard]] std::optional<std::string_view> ResolveCurrentGMSurveyAnswerText(
    const data::dbc::DbcLoader& dbc, std::uint8_t locale_index, int question_index,
    int answer_index);
[[nodiscard]] int CountCurrentGMSurveyAnswers(const data::dbc::DbcLoader& dbc,
                                              std::uint8_t locale_index,
                                              int question_index);

class GMSurveySystem {
public:

  void Reset();

  void SetQuestionData(std::uint32_t index, std::uint8_t rating, const std::string& comment);

  void SetOverallComment(const std::string& comment);

  [[nodiscard]] const GMSurveyQuestionData& GetQuestion(std::uint32_t index) const;

  [[nodiscard]] const std::string& GetOverallComment() const;

private:
  std::array<GMSurveyQuestionData, kGMSurveyMaxQuestions> questions_{};
  std::string overall_comment_;
};

}
