/*
 * LatticeMERT
 * Copyright (C)  2010-2012 
 *   Christian Buck
 *   Kārlis Goba <karlis.goba@gmail.com> 
 * 
 * LatticeMERT is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * LatticeMERT is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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

// recursively builds a partial suffix tree down to depth of bleuOrder
void BuildNGramTree(const Phrase& ref, NGramTree& tree, size_t pos,
    const size_t len, size_t depth)
{
  assert(pos<len);
  assert(depth<bleuOrder);
  Word w = ref[pos];
  if (tree.m_branches.find(w) == tree.m_branches.end())
  {
    tree.m_branches[w] = NGramTree(1);
//        cout << "adding element for w " << w << " at depth " << depth << endl;
  }
  else
  {
    tree.m_branches[w].m_count++;
  }
  if (++depth < bleuOrder && ++pos < len)
  {
    BuildNGramTree(ref, tree.m_branches[w], pos, len, depth);
  }
}

// not used any more
void ResetNGramTree(NGramTree& tree, size_t depth)
{
  tree.m_used = 0;
  if (depth == bleuOrder)
  {
    return;
  }
  for (map<Word, NGramTree>::iterator it = tree.m_branches.begin();
      it != tree.m_branches.end(); it++)
  {
    ResetNGramTree(it->second, depth + 1);
  }
}

// old implementation - not quite correct
void CountNGrams(const Phrase& cand, NGramTree& refTree, const size_t pos,
    const size_t len, const size_t depth, vector<size_t> &counts)
{
  assert(pos<len);
  assert(depth<bleuOrder);
  const Word &w = cand[pos];
  map<Word, NGramTree>::iterator it = refTree.m_branches.find(w);
  if (it == refTree.m_branches.end())
  {
    return;
  }
  NGramTree& branch = it->second;
  if (branch.m_count > branch.m_used)
  {
    counts[depth]++;
    branch.m_used++;
  }
  if (depth + 1 < bleuOrder && pos + 1 < len)
  {
    CountNGrams(cand, branch, pos + 1, len, depth + 1, counts);
  }
}

// accumulates the correct BLEU n-gram counts for given hypothesis and reference
void CountNGrams(NGramTree &hypTree, NGramTree& refTree, const size_t depth,
    vector<size_t> &counts)
{
  assert(depth<bleuOrder);
  // iterate over hypothesis n-grams
  map<Word, NGramTree>::iterator it;
  for (it = hypTree.m_branches.begin(); it != hypTree.m_branches.end(); it++)
  {
    const Word &w = it->first;
    NGramTree& hypBranch = it->second;

    // find coinciding n-grams in reference tree
    map<Word, NGramTree>::iterator it2 = refTree.m_branches.find(w);
    if (it2 == refTree.m_branches.end())
      continue;

    NGramTree& refBranch = it2->second;

    // add the the min(ref, hyp) n-gram m_count
    counts[depth] += std::min(hypBranch.m_count, refBranch.m_count);

    // recursively process (n+1)-grams
    if (depth + 1 < bleuOrder)
    {
      CountNGrams(hypBranch, refBranch, depth + 1, counts);
    }
  }
}

// accumulate counts for all hypotheses in lattice
void ComputeBleuStats(const Lattice& lattice, const vector<Line>& a,
    const Phrase& reference, vector<BleuStats>& stats)
{
  NGramTree refTree;
  for (size_t pos = 0; pos < reference.size(); ++pos)
  {
    BuildNGramTree(reference, refTree, pos, reference.size(), 0);
  }
//    cout << "Reference [" << reference << "]" << endl;
  for (size_t i = 0; i < a.size(); i++)
  {
    if (i > 0)
      ResetNGramTree(refTree, 0);
    Phrase hyp;
    a[i].GetHypothesis(lattice, hyp);
    const size_t hypSize = hyp.size();
    BleuStats lineStats(hypSize, a[i].GetLeftBound());

    NGramTree hypTree;
    for (size_t pos = 0; pos < hypSize; ++pos)
    {
      BuildNGramTree(hyp, hypTree, pos, hypSize, 0);
    }
    CountNGrams(hypTree, refTree, 0, lineStats.m_counts);

    stats.push_back(lineStats);
//        cout << "Stats for [" << hyp << "] = ";
//        cout << lineStats.counts[0] << ", ";
//        cout << lineStats.counts[1] << ", ";
//        cout << lineStats.counts[2] << ", ";
//        cout << lineStats.counts[3] << endl;
  }
}

// accumulate differences, so that they can be easily merged
void AccumulateBleu(const vector<BleuStats>& stats,
    vector<boundary>& cumulatedCounts)
{
  /* takes BleuStats data for a single sentences and appends difference vectors to cumulatedCounts */
  size_t nStats = stats.size();
  int oldCount[bleuOrder * 2] =
  { 0 };
  for (size_t i = 0; i < nStats; ++i)
  {
    vector<int> diffs(bleuOrder * 2);
    int length = stats[i].m_length;
    for (size_t n = 0; n < bleuOrder; n++)
    {
      int curr = stats[i].m_counts[n];
      diffs[n] = curr - oldCount[n];
      oldCount[n] = curr;
      int possibleNGrams = std::max(length - (int) n, 0);
      diffs[n + bleuOrder] = possibleNGrams - oldCount[n + bleuOrder];
      oldCount[n + bleuOrder] = possibleNGrams;
    }
    // no need to store length differences; length is the same as possible unigram m_count
    //diffs[bleuOrder] = length - oldLength;
    //oldLength = length;
    cumulatedCounts.push_back(boundary(stats[i].m_leftBoundary, diffs));
  }
}

double Bleu(int p[], size_t refLength)
{
  double score = 0.0;
  for (size_t n = 0; n < bleuOrder; n++)
  {
    if (p[n] == 0)
    {
      return 0.0;
    }
    // score += log((double)p[n] / (double)p[n+bleuOrder]);
    score += log((double) p[n]) - log((double) p[n + bleuOrder]);
  }
  score = score / bleuOrder;
  if (p[bleuOrder] < (int) refLength)
  {
    // apply brevity penalty
    score += 1 - (double) refLength / p[bleuOrder];
//        cout << "Applying BP (refLength=" << refLength << ", hypLength=" << p[bleuOrder] << ")" << endl;
  }
  return exp(score);
}

// sweep the axis while merging the accumulated differences and tracking BLEU score
void OptimizeBleu(vector<boundary>& cumulatedCounts, Interval& bestInterval,
    size_t refLength)
{
  std::sort(cumulatedCounts.begin(), cumulatedCounts.end());
  int p[bleuOrder * 2] =
  { 0 };
  size_t nCounts = cumulatedCounts.size();
//    cout << "considering " << nCounts << " line intersections" << endl;

  bestInterval.score = -numeric_limits<double>::infinity();
  double oldBoundary = -numeric_limits<double>::infinity();

  for (size_t i = 0; i < nCounts; i++)
  {
    double newBoundary = cumulatedCounts[i].first;
    if (oldBoundary != newBoundary)
    {
      // check if we shall update bestInterval
      double bleuScore = Bleu(p, refLength); // if this is better than the old one, that last interval was good
      // cout << "Interval [" << oldBoundary << " - " << newBoundary << "] score: " << bleuScore;
      // cout << "c: " << p[0] << " " << p[1] << " " << p[2] << " " << p[3] << " | " << p[4] << " " << p[5] << " " << p[6] << " " << p[7] << endl;
      if (bleuScore > bestInterval.score)
      {
        bestInterval.score = bleuScore;
        bestInterval.left = oldBoundary;
        bestInterval.right = newBoundary;
      }
      oldBoundary = newBoundary;
    }
    const vector<int>& currCounts = cumulatedCounts[i].second;
    for (size_t n = 0; n < bleuOrder * 2; n++)
    {
      p[n] += currCounts[n];
    }
  }
  double bleuScore = Bleu(p, refLength);
  if (bleuScore > bestInterval.score)
  {
    // This means either the last element was the best one or we only have parallel lines
    bestInterval.left = oldBoundary;
    bestInterval.right = numeric_limits<double>::infinity();
    bestInterval.score = bleuScore;
  }
  assert(bestInterval.score > -numeric_limits<double>::infinity());

}

