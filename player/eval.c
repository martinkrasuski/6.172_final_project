// Copyright (c) 2015 MIT License by 6.172 Staff

#include "./eval.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "./tbassert.h"
#include "./h_dist_table.c"

// -----------------------------------------------------------------------------
// Evaluation
// -----------------------------------------------------------------------------

typedef int32_t ev_score_t;  // Static evaluator uses "hi res" values

int RANDOMIZE;

int PCENTRAL;
int HATTACK;
int PBETWEEN;
int PCENTRAL;
int KFACE;
int KAGGRESSIVE;
int MOBILITY;
int PAWNPIN;

typedef struct heuristics_t {
  int pawnpin;
  int h_attackable;
  int mobility;
} heuristics_t;

heuristics_t * mark_laser_path_heuristics(position_t *p, color_t c, heuristics_t * heuristics);

// Heuristics for static evaluation - described in the google doc
// mentioned in the handout.


// PCENTRAL heuristic: Bonus for Pawn near center of board
ev_score_t pcentral(fil_t f, rnk_t r) {
  double df = BOARD_WIDTH/2 - f - 1;
  if (df < 0)  df = f - BOARD_WIDTH/2;
  double dr = BOARD_WIDTH/2 - r - 1;
  if (dr < 0) dr = r - BOARD_WIDTH/2;
  double bonus = 1 - sqrt(df * df + dr * dr) / (BOARD_WIDTH / sqrt(2));
  return PCENTRAL * bonus;
}


// returns true if c lies on or between a and b, which are not ordered
bool between(int c, int a, int b) {
  bool x = ((c >= a) && (c <= b)) || ((c <= a) && (c >= b));
  return x;
}

// PBETWEEN heuristic: Bonus for Pawn at (f, r) in rectangle defined by Kings at the corners
ev_score_t pbetween(position_t *p, fil_t f, rnk_t r) {
  bool is_between =
      between(f, fil_of(p->kloc[WHITE]), fil_of(p->kloc[BLACK])) &&
      between(r, rnk_of(p->kloc[WHITE]), rnk_of(p->kloc[BLACK]));
  return is_between ? PBETWEEN : 0;
}


// KFACE heuristic: bonus (or penalty) for King facing toward the other King
ev_score_t kface(position_t *p, fil_t f, rnk_t r) {
  square_t sq = square_of(f, r);
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  square_t opp_sq = p->kloc[opp_color(c)];
  int delta_fil = fil_of(opp_sq) - f;
  int delta_rnk = rnk_of(opp_sq) - r;
  int bonus;

  switch (ori_of(x)) {
    case NN:
      bonus = delta_rnk;
      break;

    case EE:
      bonus = delta_fil;
      break;

    case SS:
      bonus = -delta_rnk;
      break;

    case WW:
      bonus = -delta_fil;
      break;

    default:
      bonus = 0;
      tbassert(false, "Illegal King orientation.\n");
  }

  return (bonus * KFACE) / (abs(delta_rnk) + abs(delta_fil));
}

// KAGGRESSIVE heuristic: bonus for King with more space to back
ev_score_t kaggressive(position_t *p, fil_t f, rnk_t r) {
  square_t sq = square_of(f, r);
  piece_t x = p->board[sq];
  color_t c = color_of(x);
  tbassert(ptype_of(x) == KING, "ptype_of(x) = %d\n", ptype_of(x));

  square_t opp_sq = p->kloc[opp_color(c)];
  fil_t of = fil_of(opp_sq);
  rnk_t _or = (rnk_t) rnk_of(opp_sq);

  int delta_fil = of - f;
  int delta_rnk = _or - r;

  int bonus = 0;

  if (delta_fil >= 0 && delta_rnk >= 0) {
    bonus = (f + 1) * (r + 1);

  } else if (delta_fil <= 0 && delta_rnk >= 0) {
    bonus = (BOARD_WIDTH - f) * (r + 1);
  } else if (delta_fil <= 0 && delta_rnk <= 0) {
    bonus = (BOARD_WIDTH - f) * (BOARD_WIDTH - r);
  } else if (delta_fil >= 0 && delta_rnk <= 0) {
    bonus = (f + 1) * (BOARD_WIDTH - r);
  }

  return (KAGGRESSIVE * bonus) / (BOARD_WIDTH * BOARD_WIDTH);
}

// Marks the path of the laser until it hits a piece or goes off the board.
//
// p : current board state
// laser_map : end result will be stored here. Every square on the
//             path of the laser is marked with mark_mask
// c : color of king shooting laser
// mark_mask: what each square is marked with
void mark_laser_path(position_t *p, char *laser_map, color_t c,
                     char mark_mask) {
  position_t np = *p;

  // Fire laser, recording in laser_map
  square_t sq = np.kloc[c];
  int bdir = ori_of(np.board[sq]);

  tbassert(ptype_of(np.board[sq]) == KING,
           "ptype: %d\n", ptype_of(np.board[sq]));
  laser_map[sq] |= mark_mask;
  int beam = beam_of(bdir);

  while (true) { 
    sq += beam;
    laser_map[sq] |= mark_mask;
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    switch (ptype_of(p->board[sq])) {
      case EMPTY:  // empty square
        break;
      case PAWN:  // Pawn
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) {  // Hit back of Pawn
          return;
        }
        beam = beam_of(bdir);
        break;
      case KING:  // King
        return;  // sorry, game over my friend!
        break;
      case INVALID:  // Ran off edge of board
        return;
        break;
      default:  // Shouldna happen, man!
        tbassert(false, "Not cool, man.  Not cool.\n");
        break;
    }
  }
}

// Harmonic-ish distance: 1/(|dx|+1) + 1/(|dy|+1)
float h_dist(square_t a, square_t b) {
/*  //  printf("a = %d, FIL(a) = %d, RNK(a) = %d\n", a, FIL(a), RNK(a));
  //  printf("b = %d, FIL(b) = %d, RNK(b) = %d\n", b, FIL(b), RNK(b));
  int delta_fil = abs(fil_of(a) - fil_of(b));
  int delta_rnk = abs(rnk_of(a) - rnk_of(b));
  float x = (1.0 / (delta_fil + 1)) + (1.0 / (delta_rnk + 1));
  //  printf("max_dist = %d\n\n", x);
  return x;
*/
  return h_dist_table[a][b];
}

// Marks the path of the laser until it hits a piece or goes off the board.
//
// p : current board state
// laser_map : end result will be stored here. Every square on the
//             path of the laser is marked with mark_mask
// c : color of king shooting laser
//
// In the heuristics version of the mark_laser_path method, we are also calculating the three heuristic values
// pawnpin, mobility, and h_squares_attackable.
//
// PAWNPIN Heuristic: count number of pawns that are pinned by the
//   opposing king's laser --- and are thus immobile.
// 
// MOBILITY heuristic: safe squares around king of color opp_color(c).
//
// H_ATTACKABLE heuristic: add value the closer the laser comes to the king
// h_attackable adds the harmonic distance from a marked laser square to the enemy square
// closer the laser is to enemy king, higher the value is
heuristics_t * mark_laser_path_heuristics(position_t *p, color_t c, heuristics_t * heuristics) {
  position_t np = *p;
  square_t king_sq = p->kloc[opp_color(c)];
  
  // Initialize the h_squares_attackable value
  float h_attackable = 0;

  // Fire laser, recording in laser_map
  square_t sq = np.kloc[c];
  int bdir = ori_of(np.board[sq]);

  // Create a bounding box around king's square
  int right = fil_of(king_sq)+1;
  int left = fil_of(king_sq)-1;
  int top = rnk_of(king_sq)+1;
  int bottom = rnk_of(king_sq)-1;
  
  // Column & Row of laser Square
  int sq_rank = rnk_of(sq);
  int sq_file = fil_of(sq);

  // Check to see if the first block the laser fired in is directly surrounding the king, if so decrease mobility
  if ((sq_file <= right && sq_file >= left) && (sq_rank >= bottom && sq_rank <= top)) {
      heuristics->mobility--;
  }
  
  // Mark any invalid squares surrounding the king as not mobile
  for (int d = 0; d < 8; ++d) {
    square_t new_sq = king_sq + dir_of(d);
    if (ptype_of(p->board[new_sq]) == INVALID) {
      heuristics->mobility--;
    }
  }

  tbassert(ptype_of(np.board[sq]) == KING,
           "ptype: %d\n", ptype_of(np.board[sq]));
  int beam = beam_of(bdir);
  h_attackable += h_dist(sq, king_sq);

  while (true) { 
    sq += beam;
    sq_file = fil_of(sq);
    sq_rank = rnk_of(sq);
    tbassert(sq < ARR_SIZE && sq >= 0, "sq: %d\n", sq);

    if ((sq_file <= right && sq_file >= left) && (sq_rank >= bottom && sq_rank <= top) && ptype_of(p->board[sq]) != INVALID) {
      heuristics->mobility--;
    } 

    switch (ptype_of(p->board[sq])) {
      case EMPTY:  // empty square
        h_attackable += h_dist(sq, king_sq);
        break;
      case PAWN:  // Pawn
        h_attackable += h_dist(sq, king_sq);
        // We have hit a pawn and pinned it, increment appropriately
        if (color_of(p->board[sq]) != c) {
          heuristics->pawnpin++;
        }
        bdir = reflect_of(bdir, ori_of(p->board[sq]));
        if (bdir < 0) {  // Hit back of Pawn
          heuristics->h_attackable = h_attackable;
          return heuristics;
        }
        beam = beam_of(bdir);
        break;
      case KING:  // King
        h_attackable += h_dist(sq, king_sq);
        heuristics->h_attackable = h_attackable;
        return heuristics;  // sorry, game over my friend!
        break;
      case INVALID:  // Ran off edge of board
        heuristics->h_attackable = h_attackable;
        return heuristics;
        break;
      default:  // Shouldna happen, man!
        tbassert(false, "Not cool, man.  Not cool.\n");
        break;
    }
  }
}

// Static evaluation.  Returns score
score_t eval(position_t *p, bool verbose) {
  // seed rand_r with a value of 1, as per
  // http://linux.die.net/man/3/rand_r
  static __thread unsigned int seed = 1;
  // verbose = true: print out components of score
  ev_score_t score[2] = { 0, 0 };
  //  int corner[2][2] = { {INF, INF}, {INF, INF} };
  ev_score_t bonus;
  char buf[MAX_CHARS_IN_MOVE];
  int white_pawns = 0;
  int black_pawns = 0;

  for(int c = 0; c < 2; c++) {
    for(int i = 0; i < NUMBER_PAWNS; i++) {
      square_t sq = p->plocs[c][i];
      if(sq == 0) continue;
      fil_t f = fil_of(sq);
      rnk_t r = rnk_of(sq);
      if(c == WHITE) {
         white_pawns++;
      } else {
         black_pawns++;
      }
      // MATERIAL heuristic: Bonus for each Pawn
      bonus = PAWN_EV_VALUE;
      if (verbose) {
         printf("MATERIAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
      }
      score[c] += bonus;

      // PBETWEEN heuristic
      bonus = pbetween(p, f, r);
      if (verbose) {
        printf("PBETWEEN bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
      }
      score[c] += bonus;

      // PCENTRAL heuristic
      bonus = pcentral(f, r);
      if (verbose) {
         printf("PCENTRAL bonus %d for %s Pawn on %s\n", bonus, color_to_str(c), buf);
      }
      score[c] += bonus;
    }
  }

  for(int c = 0; c < 2; c++) {
    square_t sq = p->kloc[c];
    fil_t f = fil_of(sq);
    rnk_t r = rnk_of(sq);
    bonus = kface(p, f, r);
    if (verbose) {
      printf("KFACE bonus %d for %s King on %s\n", bonus,
      color_to_str(c), buf);
    }
    score[c] += bonus;

    // KAGGRESSIVE heuristic
    bonus = kaggressive(p, f, r);
    if (verbose) {
      printf("KAGGRESSIVE bonus %d for %s King on %s\n", bonus, color_to_str(c), buf);
    }
    score[c] += bonus;
  }

  heuristics_t white_heuristics = { .pawnpin = 0, .h_attackable = 0, .mobility = 9};
  heuristics_t * w_heuristics = &white_heuristics;

  heuristics_t black_heuristics = { .pawnpin = 0, .h_attackable = 0, .mobility = 9};
  heuristics_t * b_heuristics = &black_heuristics;
  
  // Calculate the heursitics for the white and black color
  mark_laser_path_heuristics(p, BLACK, w_heuristics);
  mark_laser_path_heuristics(p, WHITE, b_heuristics);

  ev_score_t w_hattackable = HATTACK * b_heuristics->h_attackable;
  score[WHITE] += w_hattackable;
  if (verbose) {
    printf("HATTACK bonus %d for White\n", w_hattackable);
  }
  ev_score_t b_hattackable = HATTACK * w_heuristics->h_attackable;
  score[BLACK] += b_hattackable;
  if (verbose) {
    printf("HATTACK bonus %d for Black\n", b_hattackable);
  }

  int w_mobility = MOBILITY * w_heuristics->mobility;
  score[WHITE] += w_mobility;
  if (verbose) {
    printf("MOBILITY bonus %d for White\n", w_mobility);
  }
  int b_mobility = MOBILITY * b_heuristics->mobility;
  score[BLACK] += b_mobility;
  if (verbose) {
    printf("MOBILITY bonus %d for Black\n", b_mobility);
  }

  // PAWNPIN Heuristic --- is a pawn immobilized by the enemy laser.
  int w_pawnpin = PAWNPIN * (white_pawns - w_heuristics->pawnpin);
  score[WHITE] += w_pawnpin;
  int b_pawnpin = PAWNPIN * (black_pawns - b_heuristics->pawnpin);
  score[BLACK] += b_pawnpin;

  // score from WHITE point of view
  ev_score_t tot = score[WHITE] - score[BLACK];

  if (RANDOMIZE) {
    ev_score_t  z = rand_r(&seed) % (RANDOMIZE*2+1);
    tot = tot + z - RANDOMIZE;
  }

  if (color_to_move_of(p) == BLACK) {
    tot = -tot;
  }

  return tot / EV_SCORE_RATIO;
}
