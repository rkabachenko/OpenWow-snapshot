
#include "openwow/game/gm_survey.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

namespace openwow::game {

namespace {

[[nodiscard]] std::uint32_t ResolveCurrentGMSurveyId(const data::dbc::DbcLoader& dbc,
                                                     const std::uint8_t locale_index) {
  const auto* current_survey = dbc.gm_survey_current_survey().LookupEntry(locale_index);
  if (current_survey == nullptr) {
    return 0;
  }

  return current_survey->gm_survey_id;
}

const GMSurveyQuestionData kEmptyQuestion{};

[[nodiscard]] std::optional<std::uint32_t> ResolveCurrentGMSurveyQuestionId(
    const data::dbc::DbcLoader& dbc, const std::uint8_t locale_index,
    const int question_index) {
  if (question_index < 1 ||
      question_index > static_cast<int>(kGMSurveyMaxQuestions)) {
    return std::nullopt;
  }

  const auto* survey = ResolveCurrentGMSurvey(dbc, locale_index);
  if (survey == nullptr) {
    return std::nullopt;
  }

  const auto question_id = survey->questions[static_cast<std::size_t>(question_index - 1)];
  if (question_id == 0 || dbc.gm_survey_questions().LookupEntry(question_id) == nullptr) {
    return std::nullopt;
  }

  return question_id;
}

}

const data::dbc::GMSurveySurveysEntry* ResolveCurrentGMSurvey(const data::dbc::DbcLoader& dbc,
                                                              const std::uint8_t locale_index) {
  const auto survey_id = ResolveCurrentGMSurveyId(dbc, locale_index);
  if (survey_id == 0) {
    return nullptr;
  }

  return dbc.gm_survey_surveys().LookupEntry(survey_id);
}

std::optional<std::string_view> ResolveCurrentGMSurveyQuestionText(
    const data::dbc::DbcLoader& dbc, const std::uint8_t locale_index,
    const int question_index) {
  const auto question_id = ResolveCurrentGMSurveyQuestionId(dbc, locale_index, question_index);
  if (!question_id.has_value()) {
    return std::nullopt;
  }

  const auto* question = dbc.gm_survey_questions().LookupEntry(*question_id);
  if (question == nullptr) {
    return std::nullopt;
  }

  return question->Question(locale_index);
}

std::optional<std::string_view> ResolveCurrentGMSurveyAnswerText(
    const data::dbc::DbcLoader& dbc, const std::uint8_t locale_index,
    const int question_index, const int answer_index) {
  const auto question_id = ResolveCurrentGMSurveyQuestionId(dbc, locale_index, question_index);
  if (!question_id.has_value() || answer_index < 1) {
    return std::nullopt;
  }

  const auto answer_lookup_index = static_cast<std::uint32_t>(answer_index - 1);
  for (const auto& answer : dbc.gm_survey_answers()) {
    if (answer.question_id != *question_id || answer.answer_index != answer_lookup_index) {
      continue;
    }

    return answer.Answer(locale_index);
  }

  return std::nullopt;
}

int CountCurrentGMSurveyAnswers(const data::dbc::DbcLoader& dbc,
                                const std::uint8_t locale_index,
                                const int question_index) {
  if (question_index <= 1 ||
      question_index > static_cast<int>(kGMSurveyMaxQuestions)) {
    return 0;
  }

  const auto question_id = ResolveCurrentGMSurveyQuestionId(dbc, locale_index, question_index);
  if (!question_id.has_value()) {
    return 0;
  }

  int answer_count = 0;
  for (const auto& answer : dbc.gm_survey_answers()) {
    if (answer.question_id == *question_id) {
      ++answer_count;
    }
  }

  return answer_count;
}

void GMSurveySystem::Reset() {
  for (auto& question : questions_) {
    question.rating = 0;
    question.comment.clear();
  }
  overall_comment_.clear();
}

void GMSurveySystem::SetQuestionData(std::uint32_t index, std::uint8_t rating,
                                     const std::string& comment) {
  if (index >= kGMSurveyMaxQuestions) {
    return;
  }

  questions_[index].rating = rating;
  questions_[index].comment = comment;
}

void GMSurveySystem::SetOverallComment(const std::string& comment) {
  overall_comment_ = comment;
}

const GMSurveyQuestionData& GMSurveySystem::GetQuestion(std::uint32_t index) const {
  if (index >= kGMSurveyMaxQuestions) {
    return kEmptyQuestion;
  }

  return questions_[index];
}

const std::string& GMSurveySystem::GetOverallComment() const {
  return overall_comment_;
}

}
