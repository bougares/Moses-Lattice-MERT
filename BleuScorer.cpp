#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <iostream>
#include "Types.h"
#include "BleuScorer.h"

using std::vector;
using std::numeric_limits;
using std::cout;
using std::endl;

void buildNGramTree(const Phrase& ref, NGramTree& tree, size_t pos, const size_t len, size_t depth)
{
    assert (pos<len);
    assert (depth<bleuOrder);
    Word w = ref[pos];
    if (tree.branches.find(w) == tree.branches.end()) {
        tree.branches[w] = NGramTree(1);
//        cout << "adding element for w " << w << " at depth " << depth << endl;
    } else {
        tree.branches[w].count++;
    }
    if (++depth < bleuOrder && ++pos < len) {
        buildNGramTree(ref, tree.branches[w], pos, len, depth);
    }
}

void resetNGramTree(NGramTree& tree, size_t depth)
{
    tree.used = 0;
    if (depth==bleuOrder) {
        return;
    }
    for(map<Word, NGramTree>::iterator it = tree.branches.begin(); it != tree.branches.end(); it++) {
        resetNGramTree(it->second, depth+1);
    }
}



void countNGrams(const Phrase& cand, NGramTree& refTree, const size_t pos, const size_t len, const size_t depth, vector<size_t> &counts)
{
    assert (pos<len);
    assert (depth<bleuOrder);
    const Word &w = cand[pos];
    map<Word, NGramTree>::iterator it = refTree.branches.find(w);
    if (it == refTree.branches.end()) {
        return;
    }
    NGramTree& branch = it->second;
    if (branch.count > branch.used) {
        counts[depth]++;
        branch.used++;
    }
    if (depth+1 < bleuOrder && pos+1 < len) {
        countNGrams(cand, branch, pos+1, len, depth+1, counts);
    }
}



void countNGrams(const Phrase& reference, NgramCounts& counts)
{
    size_t referenceSize = reference.size();
    size_t maxN = std::min(referenceSize, bleuOrder);
    for (size_t n=1; n<=maxN;n++) {
        for (size_t offset=0; offset+n<referenceSize+1;offset++) {
            Phrase ngram;
            for (size_t pos=offset; pos<offset+n;pos++) {
                ngram.push_back(reference[pos]);
            }
            counts[ngram]++;
        }
    }
}

void computeBleuStats(Lattice &lattice, const vector<Line>& a, const Phrase& reference, vector<BleuStats>& stats)
{
    NGramTree refTree;
    for (size_t pos=0; pos<reference.size(); ++pos) {
        buildNGramTree(reference, refTree, pos, reference.size(),0);
    }
    size_t K = a.size();
    for (size_t i = 0; i < K; i++) {
        if (i>0) resetNGramTree(refTree,0);
        Phrase hyp;
        a[i].getHypothesis(lattice, hyp);
        const size_t hypSize = hyp.size();
        BleuStats lineStats(hypSize, a[i].leftBound);
        for (size_t pos=0; pos<hypSize; ++pos) {
            countNGrams(hyp, refTree, pos, hypSize, 0, lineStats.counts);
        }
        stats.push_back(lineStats);
    }
}

void accumulateBleu(const vector<BleuStats>& stats, vector<boundary>& cumulatedCounts)
{
/* takes BleuStats data for a single sentences and appends difference vectors to cumulatedCounts */
    size_t nStats = stats.size();
    int oldCount[bleuOrder*2] = {0};
    int oldLength = 0;
    for (size_t i=0;i<nStats;++i) {
        vector<int> diffs((bleuOrder + 1)*2);
        int length = stats[i].length;
        for (size_t n =0; n<bleuOrder;n++) {
            int curr = stats[i].counts[n];
            diffs[n] = curr - oldCount[n];
            oldCount[n] = curr;
            int possibleNGrams = std::max(length-(int)n, 0);
            diffs[n+bleuOrder] = possibleNGrams - oldCount[n+bleuOrder];
            oldCount[n+bleuOrder] = possibleNGrams;
        }
        diffs[bleuOrder] = length - oldLength;
        oldLength = length;
        cumulatedCounts.push_back( boundary(stats[i].leftBoundary,diffs) );
    }
}


double Bleu(int p[], size_t refLength)
{
    double score = 0.0;
    for (size_t n=0; n<bleuOrder; n++) {
        if (p[n] == 0) {
            return 0.0;
        }
        // score += log((double)p[n] / (double)p[n+bleuOrder]);
        score += log((double)p[n]) - log((double)p[n+bleuOrder]);
    }
    score = score / bleuOrder;
    if (p[bleuOrder] < (int)refLength) {
        // apply brevity penalty
        score += 1 - (double)refLength / p[bleuOrder];
//        cout << "Applying BP (refLength=" << refLength << ", hypLength=" << p[bleuOrder] << ")" << endl;
    }
    return exp(score);
}

void optimizeBleu(vector<boundary>& cumulatedCounts, Interval& bestInterval, size_t refLength)
{
    std::sort(cumulatedCounts.begin(), cumulatedCounts.end());
    int p[bleuOrder*2] = {0};
    size_t nCounts = cumulatedCounts.size();
    cout << "considering " << nCounts << " line intersections" << endl;

    bestInterval.score = -numeric_limits<double>::infinity();
    double oldBoundary = -numeric_limits<double>::infinity();

    for (size_t i=0; i<nCounts; i++) {
        double newBoundary = cumulatedCounts[i].first;
        if (oldBoundary != newBoundary) {
            // check if we shall update bestInterval
            double bleuScore = Bleu(p, refLength);  // if this is better than the old one, that last interval was good
            // cout << "Interval [" << oldBoundary << " - " << newBoundary << "] score: " << bleuScore;
            // cout << "c: " << p[0] << " " << p[1] << " " << p[2] << " " << p[3] << " | " << p[4] << " " << p[5] << " " << p[6] << " " << p[7] << endl;
            if (bleuScore > bestInterval.score) {
                bestInterval.score = bleuScore;
                bestInterval.left = oldBoundary;
                bestInterval.right = newBoundary;
            }
            oldBoundary = newBoundary;
        }
        const vector<int>& currCounts = cumulatedCounts[i].second;
        for (size_t n=0; n<bleuOrder*2; n++) {
            p[n] += currCounts[n];
        }
    }
    double bleuScore = Bleu(p, refLength);
    if (bleuScore > bestInterval.score) {
        // This means either the last element was the best one or we only have parallel lines
        bestInterval.left = oldBoundary;
        bestInterval.right = numeric_limits<double>::infinity();
        bestInterval.score = bleuScore;
    }
    assert (bestInterval.score > -numeric_limits<double>::infinity());
    cout << "Final BestInterval [" << bestInterval.left << " - " << bestInterval.right << "] score: " << bestInterval.score << endl;

}

