
#include "stdafx.h"
#include "0x88_math.h"
#include "eval.h"
#include "transposition.h"

/******************************************************************************
*  We want our eval to be color-independent, i.e. the same functions ought to *
*  be called for white and black pieces. This requires some way of converting *
*  row and square coordinates.                                                *
******************************************************************************/

static const int seventh[2] = { ROW(A7), ROW(A2) };
static const int eighth[2] = { ROW(A8), ROW(A1) };
static const int stepFwd[2] = { NORTH, SOUTH };
static const int stepBck[2] = { SOUTH, NORTH };

static const int inv_sq[128] = {
      A8, B8, C8, D8, E8, F8, G8, H8, -1, -1, -1, -1, -1, -1, -1, -1,
    A7, B7, C7, D7, E7, F7, G7, H7, -1, -1, -1, -1, -1, -1, -1, -1,
    A6, B6, C6, D6, E6, F6, G6, H6, -1, -1, -1, -1, -1, -1, -1, -1,
    A5, B5, C5, D5, E5, F5, G5, H5, -1, -1, -1, -1, -1, -1, -1, -1,
    A4, B4, C4, D4, E4, F4, G4, H4, -1, -1, -1, -1, -1, -1, -1, -1,
    A3, B3, C3, D3, E3, F3, G3, H3, -1, -1, -1, -1, -1, -1, -1, -1,
    A2, B2, C2, D2, E2, F2, G2, H2, -1, -1, -1, -1, -1, -1, -1, -1,
    A1, B1, C1, D1, E1, F1, G1, H1, -1, -1, -1, -1, -1, -1, -1, -1
};

#define REL_SQ(cl, sq)       ((cl) == (WHITE) ? (sq) : (inv_sq[sq]))

/* adjustements of piece value based on the number of own pawns */
int n_adj[9] = { -20, -16, -12, -8, -4,  0,  4,  8, 12 };
int r_adj[9] = { 15,  12,   9,  6,  3,  0, -3, -6, -9 };

static const int SafetyTable[100] = {
     0,  0,   1,   2,   3,   5,   7,   9,  12,  15,
    18,  22,  26,  30,  35,  39,  44,  50,  56,  62,
    68,  75,  82,  85,  89,  97, 105, 113, 122, 131,
    140, 150, 169, 180, 191, 202, 213, 225, 237, 248,
    260, 272, 283, 295, 307, 319, 330, 342, 354, 366,
    377, 389, 401, 412, 424, 436, 448, 459, 471, 483,
    494, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500
};

/******************************************************************************
*  This struct holds data about certain aspects of evaluation, which allows   *
*  our program to print them if desired.                                      *
******************************************************************************/

struct eval_vector {
  int gamePhase;   // function of piece material: 24 in opening, 0 in endgame
  int mgMob[2];     // midgame mobility
  int egMob[2];     // endgame mobility
  int attCnt[2];    // no. of pieces attacking zone around enemy king
  int attWeight[2]; // weight of attacking pieces - index to SafetyTable
  int mgTropism[2]; // midgame king tropism score
  int egTropism[2]; // endgame king tropism score
  int kingShield[2];
  int adjustMaterial[2];
  int blockages[2];
  int positionalThemes[2];
} v;

int eval(int alpha, int beta, int use_hash) {
  int result = 0, mgScore = 0, egScore = 0;
  int stronger, weaker;

  /**************************************************************************
  *  Probe the evaluatinon hashtable, unless we call eval() only in order   *
*  to display detailed result                                             *
  **************************************************************************/

  int probeval = tteval_probe();
  if (probeval != INVALID && use_hash)
    return probeval;

  /**************************************************************************
  *  Clear all eval data                                                    *
  **************************************************************************/

  v.gamePhase = b.piece_cnt[WHITE][KNIGHT] + b.piece_cnt[WHITE][BISHOP] + 2 * b.piece_cnt[WHITE][ROOK] + 4 * b.piece_cnt[WHITE][QUEEN]
    + b.piece_cnt[BLACK][KNIGHT] + b.piece_cnt[BLACK][BISHOP] + 2 * b.piece_cnt[BLACK][ROOK] + 4 * b.piece_cnt[BLACK][QUEEN];

  for (int side = 0; side <= 1; side++) {
    v.mgMob[side] = 0;
    v.egMob[side] = 0;
    v.attCnt[side] = 0;
    v.attWeight[side] = 0;
    v.mgTropism[side] = 0;
    v.egTropism[side] = 0;
    v.adjustMaterial[side] = 0;
    v.blockages[side] = 0;
    v.positionalThemes[side] = 0;
    v.kingShield[side] = 0;
  }

  /**************************************************************************
*  Sum the incrementally counted material and piece/square table values   *
**************************************************************************/

  mgScore = b.piece_material[WHITE] + b.pawn_material[WHITE] + b.pcsq_mg[WHITE]
    - b.piece_material[BLACK] - b.pawn_material[BLACK] - b.pcsq_mg[BLACK];
  egScore = b.piece_material[WHITE] + b.pawn_material[WHITE] + b.pcsq_eg[WHITE]
    - b.piece_material[BLACK] - b.pawn_material[BLACK] - b.pcsq_eg[BLACK];

  /**************************************************************************
* add king's pawn shield score and evaluate part of piece blockage score  *
  * (the rest of the latter will be done via piece eval)                    *
**************************************************************************/

  v.kingShield[WHITE] = wKingShield();
  v.kingShield[BLACK] = bKingShield();
  blockedPieces(WHITE);
  blockedPieces(BLACK);
  mgScore += (v.kingShield[WHITE] - v.kingShield[BLACK]);

  /* tempo bonus */
  if (b.stm == WHITE) result += e.TEMPO;
  else				  result -= e.TEMPO;

  /**************************************************************************
  *  Adjusting material value for the various combinations of pieces.       *
  *  Currently it scores bishop, knight and rook pairs. The first one       *
  *  gets a bonus, the latter two - a penalty. Beside that knights lose     *
*  value as pawns disappear, whereas rooks gain.                          *
  **************************************************************************/

  if (b.piece_cnt[WHITE][BISHOP] > 1) v.adjustMaterial[WHITE] += e.BISHOP_PAIR;
  if (b.piece_cnt[BLACK][BISHOP] > 1) v.adjustMaterial[BLACK] += e.BISHOP_PAIR;
  if (b.piece_cnt[WHITE][KNIGHT] > 1) v.adjustMaterial[WHITE] -= e.P_KNIGHT_PAIR;
  if (b.piece_cnt[BLACK][KNIGHT] > 1) v.adjustMaterial[BLACK] -= e.P_KNIGHT_PAIR;
  if (b.piece_cnt[WHITE][ROOK] > 1) v.adjustMaterial[WHITE] -= e.P_ROOK_PAIR;
  if (b.piece_cnt[BLACK][ROOK] > 1) v.adjustMaterial[BLACK] -= e.P_ROOK_PAIR;

  v.adjustMaterial[WHITE] += n_adj[b.piece_cnt[WHITE][PAWN]] * b.piece_cnt[WHITE][KNIGHT];
  v.adjustMaterial[BLACK] += n_adj[b.piece_cnt[BLACK][PAWN]] * b.piece_cnt[BLACK][KNIGHT];
  v.adjustMaterial[WHITE] += r_adj[b.piece_cnt[WHITE][PAWN]] * b.piece_cnt[WHITE][ROOK];
  v.adjustMaterial[BLACK] += r_adj[b.piece_cnt[BLACK][PAWN]] * b.piece_cnt[BLACK][ROOK];

  result += getPawnScore();

  /**************************************************************************
  *  Evaluate pieces                                                        *
  **************************************************************************/

  for (U8 row = 0; row < 8; row++)
    for (U8 col = 0; col < 8; col++) {

      S8 sq = SET_SQ(row, col);

      if (b.color[sq] != COLOR_EMPTY) {
        switch (b.pieces[sq]) {
          case PAWN: // pawns are evaluated separately
            break;
          case KNIGHT:
            EvalKnight(sq, b.color[sq]);
            break;
          case BISHOP:
            EvalBishop(sq, b.color[sq]);
            break;
          case ROOK:
            EvalRook(sq, b.color[sq]);
            break;
          case QUEEN:
            EvalQueen(sq, b.color[sq]);
            break;
          case KING:
            break;
        }
      }
    }

  /**************************************************************************
  *  Merge  midgame  and endgame score. We interpolate between  these  two  *
  *  values, using a gamePhase value, based on remaining piece material on  *
*  both sides. With less pieces, endgame score becomes more influential.  *
  **************************************************************************/

  mgScore += (v.mgMob[WHITE] - v.mgMob[BLACK]);
  egScore += (v.egMob[WHITE] - v.egMob[BLACK]);
  mgScore += (v.mgTropism[WHITE] - v.mgTropism[BLACK]);
  egScore += (v.egTropism[WHITE] - v.egTropism[BLACK]);
  if (v.gamePhase > 24) v.gamePhase = 24;
  int mgWeight = v.gamePhase;
  int egWeight = 24 - mgWeight;
  result += ((mgScore * mgWeight) + (egScore * egWeight)) / 24;

  /**************************************************************************
  *  Add phase-independent score components.                                *
  **************************************************************************/

  result += (v.blockages[WHITE] - v.blockages[BLACK]);
  result += (v.positionalThemes[WHITE] - v.positionalThemes[BLACK]);
  result += (v.adjustMaterial[WHITE] - v.adjustMaterial[BLACK]);

  /**************************************************************************
  *  Merge king attack score. We don't apply this value if there are less   *
  *  than two attackers or if the attacker has no queen.                    *
  **************************************************************************/

  if (v.attCnt[WHITE] < 2 || b.piece_cnt[WHITE][QUEEN] == 0) v.attWeight[WHITE] = 0;
  if (v.attCnt[BLACK] < 2 || b.piece_cnt[BLACK][QUEEN] == 0) v.attWeight[BLACK] = 0;
  result += SafetyTable[v.attWeight[WHITE]];
  result -= SafetyTable[v.attWeight[BLACK]];

  /**************************************************************************
  *  Low material correction - guarding against an illusory material advan- *
  *  tage. Full blown program should have more such rules, but the current  *
  *  set ought to be useful enough. Please note that our code  assumes      *
*  different material values for bishop and  knight.                      *
  *                                                                         *
  *  - a single minor piece cannot win                                      *
  *  - two knights cannot checkmate bare king                               *
  *  - bare rook vs minor piece is drawish                                  *
  *  - rook and minor vs rook is drawish                                    *
  **************************************************************************/

  if (result > 0) {
    stronger = WHITE;
    weaker = BLACK;
  }
  else {
    stronger = BLACK;
    weaker = WHITE;
  }

  if (b.pawn_material[stronger] == 0) {

    if (b.piece_material[stronger] < 400) return 0;

    if (b.pawn_material[weaker] == 0
        && (b.piece_material[stronger] == 2 * e.PIECE_VALUE[KNIGHT]))
      return 0;

    if (b.piece_material[stronger] == e.PIECE_VALUE[ROOK]
        && b.piece_material[weaker] == e.PIECE_VALUE[BISHOP]) result /= 2;

    if (b.piece_material[stronger] == e.PIECE_VALUE[ROOK]
        && b.piece_material[weaker] == e.PIECE_VALUE[KNIGHT]) result /= 2;

    if (b.piece_material[stronger] == e.PIECE_VALUE[ROOK] + e.PIECE_VALUE[BISHOP]
        && b.piece_material[weaker] == e.PIECE_VALUE[ROOK]) result /= 2;

    if (b.piece_material[stronger] == e.PIECE_VALUE[ROOK] + e.PIECE_VALUE[KNIGHT]
        && b.piece_material[weaker] == e.PIECE_VALUE[ROOK]) result /= 2;
  }

  /**************************************************************************
  *  Finally return the score relative to the side to move.                 *
  **************************************************************************/

  if (b.stm == BLACK) result = -result;

  tteval_save(result);

  return result;
}




