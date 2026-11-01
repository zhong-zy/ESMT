
#ifndef SHRUBS_LIKE_INDEX_SCORE_H
#define SHRUBS_LIKE_INDEX_SCORE_H

namespace workloadml {

    struct Score {
        double score1=0;
        double score2=0;

        Score() = default;

        Score(double score1, double score2=0): score1(score1), score2(score2) {}

        bool operator==(Score &b) const {
            return this->score1 == b.score1 && this->score2 == b.score2;
        }
        bool operator>(Score &b) const {
            if (this->score1 != b.score1) {
                return this->score1 > b.score1;
            }
            return this->score2 > b.score2;
        }
        bool operator<(Score &b) const {
            if (this->score1 != b.score1) {
                return this->score1 < b.score1;
            }
            return this->score2 < b.score2;
        }
    };
}

#endif
