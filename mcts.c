#include "mcts.h"

/* Hyper-parameters */
gdouble RHO = 1.414;  //sqrt(2)
gint MIN_SAMPLES = 1;
gint MAX_SAMPLES = 100;
gint CONEX_TIMEOUT = 0;
gboolean PERSISTENT = FALSE;
gboolean COVERAGE_ONLY = TRUE;
enum score_function SCORE_FUNCTION = UCT;

/* ============================================== TreeNode Functions ============================================== */

TreeNodeData* new_tree_node_data (int response_code, enum node_colour colour)
{
    TreeNodeData* tree_node_data;
    tree_node_data = g_new(TreeNodeData, 1); //  allocates 1 element of TreeNode

    // set property
    tree_node_data->id = response_code;
    tree_node_data->path = NULL;
    tree_node_data->path_len = 0;
    //NOTE: This is probably not needed, left it here in case it comes in handy later.
    tree_node_data->colour = colour;

    // set statistics
    tree_node_data->selected = 0;
    tree_node_data->discovered = 0;
    // tree_node_data->stats.selected_seed_index = 0;
    tree_node_data->seeds = NULL;
    tree_node_data->seeds_count = 0;

    //TODO: Detect terminations (i.e. leaves) with some response code, and mark them fully explored.
    // e.g. predefine a set of termination code
    //TOASK: Given the last node of a past communication, would it have any child if we give it more inputs?
    tree_node_data->fully_explored = FALSE;
    //NOTE: No way to know if we have found all possible inputs to fuzz a node.
    tree_node_data->exhausted = FALSE;

    return tree_node_data;
}


TreeNode* new_tree_node(TreeNodeData* tree_data)
{
    GNode* tree_node;
    tree_node = g_node_new (tree_data);
    return tree_node;
}


TreeNodeData* get_tree_node_data(TreeNode* tree_node)
{
    return (TreeNodeData*) tree_node->data;
}

TreeNode* get_simulation_child(TreeNode* tree_node)
{
  TreeNodeData* node_data = get_tree_node_data(tree_node);
  TreeNode* sim_child = node_data->simulation_child;

  assert(node_data->colour != Golden);
  assert(get_tree_node_data(sim_child)->colour == Golden);

  return sim_child;
}

double tree_node_exploitation_score(TreeNode* tree_node)
{
    TreeNodeData* node_data = get_tree_node_data(tree_node);

    if (!node_data->selected) return INFINITY;

    g_assert(node_data->selected);
//    g_printf("%u / %u = %lf\n",
//             get_tree_node_data(tree_node)->stats.paths_discovered,
//             get_tree_node_data(tree_node)->stats.selected_times,
//             (double) get_tree_node_data(tree_node)->stats.paths_discovered / get_tree_node_data(tree_node)->stats.selected_times);
    return (double) node_data->discovered / node_data->selected;
}

double seed_exploitation_score(TreeNode* tree_node, int seed_index)
{
    seed_info_t* target_seed = get_tree_node_data(tree_node)->seeds[seed_index];
    return (double) target_seed->discovered / target_seed->selected;
}

double tree_node_exploration_score(TreeNode* tree_node)
{
    if (G_NODE_IS_ROOT(tree_node)) return INFINITY;
    g_assert(tree_node->parent);
    TreeNodeData* node_data = get_tree_node_data(tree_node);
    TreeNodeData* parent_data = get_tree_node_data(tree_node->parent);
//    g_printf("%lf * sqrt(2*log(%u)/%u)\n",
//             RHO,
//             get_tree_node_data(tree_node->parent)->stats.selected_times,
//             get_tree_node_data(tree_node)->stats.selected_times);
    return  RHO * sqrt(2 * log((double) parent_data->selected) / node_data->selected);
}

double seed_exploration_score(TreeNode* tree_node, int seed_index)
{
    seed_info_t* target_seed = get_tree_node_data(tree_node)->seeds[seed_index];
    TreeNodeData* node_data = get_tree_node_data(tree_node);
    return RHO * sqrt(2 * log((double)node_data->selected)/target_seed->selected);
}

double tree_node_score(TreeNode* tree_node)
{
    /*
     * If using the random score function, return a random number
     * Else compute the score of a node:
     *  1. If the node is fully explored, return -INFINITY
     *  2. If the node is root, return INFINITY
     *  3. If the node fits the fish bone optimisation, return -INFINITY
     *  4. If the node has not been selected before, return INFINITY
     *  5. If the node has been selected before, apply the UCT function
     */
    if (SCORE_FUNCTION == Random) return g_rand_int(RANDOM_NUMBER_GENERATOR);

    if (get_tree_node_data(tree_node)->fully_explored) return -INFINITY;

    if (G_NODE_IS_ROOT(tree_node))  return INFINITY;

    // if (fits_fish_bone_optimisation(tree_node)) return -INFINITY;

    if (!get_tree_node_data(tree_node)->selected)  return INFINITY;

    double exploit_score = tree_node_exploitation_score(tree_node);
    double explore_score = tree_node_exploration_score(tree_node);

    return exploit_score + explore_score;
}

double seed_score(TreeNode* tree_node, int seed_index)
{
    // return g_rand_int(RANDOM_NUMBER_GENERATOR);

    if (SCORE_FUNCTION == Random) return g_rand_int(RANDOM_NUMBER_GENERATOR);

    seed_info_t* target_seed = get_tree_node_data(tree_node)->seeds[seed_index];

    if (!target_seed->selected)  return INFINITY;

    double exploit_score = seed_exploitation_score(tree_node, seed_index);
    double explore_score = seed_exploration_score(tree_node, seed_index);

    return exploit_score + explore_score;
}

//gboolean is_fully_explored(TreeNode *  tree_node)
//{
//    /*
//     * Check if a node is fully explored:
//     *  1. If not in PERSISTENT mode, return the exact stats of the tree node
//     *  2. If in PERSISTENT mode, but the root is not fully explored, return the exact stats
//     *  3. If in PERSISTENT mode and the root is fully explored, but the node is root, return the exact stats
//     *  4. Else a node is fully explored if it is a non-Golden leaf, or it is exhausted
//     */
//    TreeNodeData * tree_node_data = get_tree_node_data(tree_node);
//    if (!PERSISTENT)    return tree_node_data->fully_explored;
//
//    if (!get_tree_node_data(g_node_get_root(tree_node))->fully_explored)  return tree_node_data->fully_explored;
//
//    if (G_NODE_IS_ROOT(tree_node))  return tree_node_data->fully_explored;
//
//    return (is_leaf(tree_node) && tree_node_data->colour != Golden) || tree_node_data->exhausted;
//}

gboolean is_leaf(TreeNode* tree_node) {
    /*
     * If the node is a phantom, then it is not a leaf
     * If the node has no child, it is a leaf
     * If the node has only a simulation child, it is a leaf
     * Else it is not
     */
    return (get_tree_node_data(tree_node)->colour != Purple)
           && (G_NODE_IS_LEAF(tree_node)
               || ((g_node_n_children(tree_node) == 1) &&
                   get_tree_node_data(g_node_first_child(tree_node))->colour == Golden));
}

//gboolean fits_fish_bone_optimisation(TreeNode * tree_node)
//{
//    /* NOTE: We might not be able to know for sure if a node/subtree is fully explored.
//     *  One optimisation is to predefine a set of termination response codes.
//     * Fish bone optimisation: if a simulation child
//     * has only one sibling X who is not fully explored,
//     * and X is not white (so that all siblings are found)
//     * then do not simulate from that simulation child but only from X
//     * as all new paths can only come from X
//     */
////    gboolean is_simul = (get_tree_node_data(tree_node)->colour == Golden);
//
//    return FALSE;
//}

TreeNode* best_child(TreeNode* tree_node)
{
    gdouble max_score = -INFINITY;
    gint number_of_children = g_node_n_children(tree_node);
    gint number_of_ties = 0;
    gint ties[number_of_children];

    if (number_of_children == 1) {
//        g_printf("Only one child: ");
//        tree_node_print(g_node_nth_child(tree_node, 0));
        return g_node_nth_child(tree_node, 0);
    }

    for (gint child_index = 0; child_index < number_of_children; child_index++) {
        gdouble score = tree_node_score(g_node_nth_child(tree_node, child_index));
//        g_printf("max score: %lf, current score: %lf\n", max_score, score);
        if (score < max_score) continue;
        if (score > max_score) number_of_ties = 0;
        max_score = score;
        ties[number_of_ties] = child_index;
        number_of_ties ++;
//        g_printf("max score: %lf, current score: %lf, number of ties: %d, current child index: %d\n",
//               max_score, score, number_of_ties, child_index);
    }

    if (number_of_ties == 1)
    {
//        g_printf("Only one candidate: ");
//        tree_node_print(g_node_nth_child(tree_node, ties[0]));
        return g_node_nth_child(tree_node, ties[0]);
    }

//    g_printf("Best child has %d candidates: \n", number_of_ties);
//    for (int i = 0; i < number_of_ties; ++i) {
//        tree_node_print(g_node_nth_child(tree_node, ties[i]));
//    }
//    g_printf("\n");
    return g_node_nth_child(tree_node, ties[g_rand_int_range(RANDOM_NUMBER_GENERATOR, 0, number_of_ties)]);
}

seed_info_t* best_seed(TreeNode* tree_node)
{
    gdouble max_score = -INFINITY;
    gint number_of_seeds = get_tree_node_data(tree_node)->seeds_count;
    gint number_of_ties = 0;
    gint ties[number_of_seeds];

    if (number_of_seeds == 1) {
        g_printf("Only one seed: ");
        return get_tree_node_data(tree_node)->seeds[0];
    }

    for (gint seed_index = 0; seed_index < number_of_seeds; seed_index++) {
        int score = seed_score(tree_node, seed_index);

        if (score < max_score) continue;
        if (score > max_score) number_of_ties = 0;
        max_score = score;
        ties[number_of_ties] = seed_index;
        number_of_ties ++;
    }

    if (number_of_ties == 1)
    {
        g_printf("Only one candidate: ");
        return get_tree_node_data(tree_node)->seeds[ties[0]];
    }

    int winner_index = ties[g_rand_int_range(RANDOM_NUMBER_GENERATOR, 0, number_of_ties)];
    return get_tree_node_data(tree_node)->seeds[winner_index];
}

TreeNode* exists_child(TreeNode* tree_node, int target_response_code)
{
    TreeNode* child_node = g_node_first_child(tree_node);

    while (child_node)
    {
        // tree_node_print(child_node);
        if (get_tree_node_data(child_node)->id == target_response_code)  return child_node;
        child_node = g_node_next_sibling(child_node);
    }
    return NULL;
}

TreeNode* append_child(TreeNode* tree_node, int child_response_code, enum node_colour colour)
{
    TreeNode* child = g_node_append_data(tree_node, new_tree_node_data(child_response_code, colour));
    if (colour != Golden) get_tree_node_data(child)->simulation_child = append_child(child, -1, Golden);

    return child;
}

void print_reversed_path(TreeNode* tree_node)
{
    do {
        g_printf("%d ", get_tree_node_data(tree_node)->id);
        tree_node = tree_node->parent;
    } while (tree_node);
    g_printf("\n");
}

u32* collect_node_path(TreeNode* tree_node, u32* path_len)
{
  u32* path = NULL;
  u32* reversed_path = NULL;

  *path_len = 0;
  u32 path_size = 0;

  //NOTE: If the tree_node is Golden, then its path is the same as its parent
  if (get_tree_node_data(tree_node)->colour == Golden) {
    assert(tree_node->parent);
    tree_node = tree_node->parent;
    assert(get_tree_node_data(tree_node)->colour != Golden);
  }

  // NOTE: The condition is tree_node->parent instead of tree_node,
  //  because we don't want to include the id of ROOT in the path,
  //  given the id of ROOT is always 0 and does not represent any valid response code.
  while (tree_node->parent) {
    //NOTE: dynamically expanding the size of array
    // g_printf("%d of %d: %d\n", *path_len, path_size, (*path_len) >= (path_size));
    if ((*path_len) >= (path_size)) {
      path_size += 100;
//      g_printf("%d\n", path_size);
      u32* new_reversed_path = ck_alloc(path_size * sizeof(u32));
      if (new_reversed_path) {
        for (u32 i = 0; i < *path_len; ++i) {
          new_reversed_path[i] = reversed_path[i];
        }
        // TOASK: free causes error
//                ck_free(reversed_path);
//                int * reversed_path = new_reversed_path;
        reversed_path = new_reversed_path;
      }
      else {
        g_print("Insufficient Memory when collecting path");
        exit(0);
      }
    }

    // collect addresses from leaf to root
    reversed_path[*path_len] = get_tree_node_data(tree_node)->id;
    *path_len += 1;
    tree_node = tree_node->parent;
  }
//    g_printf("%d of %d\n", *path_len, path_size);

//    for (u32 i = 0; i < *path_len; i++) {
//        g_printf("%d ", reversed_path[i]);
//    }

//    return reversed_path;
  // TOASK: free causes error
  // ck_free(reversed_path);

  path = ck_alloc((*path_len) * sizeof(u32));
  for (u32 i = 0; i < *path_len; i++) {
    path[i] = reversed_path[(*path_len) - i - 1];
  }
  return path;
}

void print_path(TreeNode* tree_node)
{
    u32 path_len;
    u32* path = collect_node_path(tree_node, &path_len);
    for (u32 i = 0; i < path_len; i++) {
        g_printf("%d ", path[i]);
    }
    g_print("\n");
}

void prepare_path_str(TreeNode* tree_node)
{
  u32 path_len = 0;
  u32* path = collect_node_path(tree_node, &path_len);

  u32 path_str_len = 0;
  for (u32 i = 0; i < path_len; i++) {
    path_str_len += (int)((ceil(log10(path[i]))+1)*sizeof(char));
  }

  log_info("Node path len: %d", path_len);
  for (int i = 0; i < path_len; ++i) {

    log_info("Node path (%d/%d): %d", i, path_len, path[i]);
    g_printf("%d, ", path[i]);
    printf("%d, ", path[i]);
  }

  u32 n = 0, m = 0;

  char path_str[100] = {0};
  for (u32 i = 0; i < path_len; i++) {
    m = snprintf(&path_str[n], 100-n, "%d", path[i]);
    if (m<0) {
      exit(1);
    }
    n += m;
  }

//  g_printf("%s", path_str);
  log_info("Node path: %s", path_str);
}

int colour_encoder(enum node_colour colour) {
    int colour_code = 0;
    switch (colour) {
        case White:
            colour_code = 37;
            break;
        case Red:
            colour_code = 31;
            break;
        case Golden:
            colour_code = 33;
            break;
        case Purple:
            colour_code = 35;
            break;
        case Black:
            colour_code = 30;
            break;
    }
    assert(colour_code);
    return colour_code;
}

void tree_print(TreeNode* tree_node, TreeNode* mark_node, int indent, int found)
{
    char* layer = malloc(200);
    for (int i = 0; i < indent-1; ++i) strcat(layer, "|  ");
    if (indent) strcat(layer, "|-- ");
    strcat(layer, tree_node_repr(tree_node));
    if (tree_node == mark_node) strcat(layer, "\033[1;32m <=< found\033[0;m");
    strcat(layer, "\n");
    log_info(layer);
    if (g_node_n_children(tree_node)) indent++;
    for (int i = 0; i < g_node_n_children(tree_node); ++i) {
        tree_print(g_node_nth_child(tree_node, i), mark_node, indent, found);
    }
}

void tree_log(TreeNode* tree_node, TreeNode* mark_node, int indent, int found)
{
//    log_Message* message = (log_Message*) message_init();
    char* message = NULL;

    for (int i = 0; i < indent-1; ++i) message_format(&message, "|  ");
    if (indent) message_format(&message, "|-- ");
    message_format(&message, tree_node_repr(tree_node));
    if (tree_node == mark_node) message_format(&message, "\033[1;32m <=< found\033[0;m");
//    message_format(message, "\n");
    log_info(message);
    if (g_node_n_children(tree_node)) indent++;
    for (int i = 0; i < g_node_n_children(tree_node); ++i) {
      tree_log(g_node_nth_child(tree_node, i), mark_node, indent, found);
    }
}

char* tree_node_repr(TreeNode* tree_node)
{
  TreeNodeData* tree_node_data = get_tree_node_data(tree_node);
//  log_Message* message = message_init();
  char* message = NULL;
//  message_format(message, "id: %u", tree_node_data->id);
  message_format(&message, "res_code: %u, score: %lf (%lf + %lf)",
           colour_encoder(tree_node_data->colour),
           tree_node_data->id,
           tree_node_score(tree_node),
           tree_node_exploitation_score(tree_node),
           tree_node_exploration_score(tree_node));

//  char* repr = malloc(100);
//  sprintf(repr, "\033[1;%dmres_code: %u, score: %lf (%lf + %lf) \033[0m",
//           colour_encoder(tree_node_data->colour),
//           tree_node_data->id,
//           tree_node_score(tree_node),
//           tree_node_exploitation_score(tree_node),
//           tree_node_exploration_score(tree_node));
  return message;
}

void tree_node_log(TreeNode* tree_node)
{
  TreeNodeData* tree_node_data = get_tree_node_data(tree_node);
  log_info("\033[1;%dmres_code: %u, score: %lf (%lf + %lf) \033[0m",
           colour_encoder(tree_node_data->colour),
           tree_node_data->id,
           tree_node_score(tree_node),
           tree_node_exploitation_score(tree_node),
           tree_node_exploration_score(tree_node));
}

void tree_node_print(TreeNode* tree_node)
{
    TreeNodeData* tree_node_data = get_tree_node_data(tree_node);
    g_printf ("\033[1;%dmres_code: %u, score: %lf (%lf + %lf) \033[0m",
              colour_encoder(tree_node_data->colour),
              tree_node_data->id,
              tree_node_score(tree_node),
              tree_node_exploitation_score(tree_node),
              tree_node_exploration_score(tree_node));
}

//
//void
//tree_node_free (TreeNode * tree_node)
//{
//    g_free (tree_node->input_prefix); /* causes segfault*/
//    g_free (tree_node);
//}
//
//
//gboolean
//traverse_free_func (GNode * gnode, gpointer data)
//{
//    tree_node_free (TreeNode(gnode->data));
//    return FALSE;
//}


seed_info_t* construct_seed_with_queue_entry(void *q)
{
  seed_info_t* seed = (seed_info_t*) ck_alloc (sizeof(seed_info_t));
  seed->q = q;
  seed->selected = 0;
  seed->discovered = 0;
  return seed;
}

void add_seed_to_node(seed_info_t* seed, TreeNode * node)
{
  assert(get_tree_node_data(node)->colour == Golden);

  // TOASK: Should we allocate more spaces to avoid repeated reallocation?
  TreeNodeData* node_data = get_tree_node_data(node);
  node_data->seeds = (void **) ck_realloc (node_data->seeds, (node_data->seeds_count + 1) * sizeof(void *));
  node_data->seeds[node_data->seeds_count] = (void *) seed;
  node_data->seeds_count++;
}

u32* collect_region_path(region_t region, u32* path_len)
{
    *path_len = region.state_count;
    return region.state_sequence;
}

/* ============================================== TreeNode Functions ============================================== */
/* ================================================ MCTS Functions ================================================ */

TreeNode* select_tree_node(TreeNode* parent_tree_node)
{
    while (get_tree_node_data(parent_tree_node)->colour != Golden) {
//        tree_node_print(parent_tree_node);
//        g_print("\n");
        parent_tree_node = best_child(parent_tree_node);
        /* NOTE: Selected stats propagation of nodes along the selection path is done here */
        get_tree_node_data(parent_tree_node)->selected++;
        assert(parent_tree_node);
    }
    return parent_tree_node;
}


seed_info_t* select_seed(TreeNode* tree_node_selected)
{
    seed_info_t* seed = best_seed(tree_node_selected);
    /* NOTE: Selected stats propagation of the seed is done here */
    seed->selected++;
    return seed;
}


TreeNode* Initialisation()
{
    TreeNode* root = new_tree_node(new_tree_node_data(0, White));
    get_tree_node_data(root)->simulation_child = append_child(root, -1, Golden);
    log_add_fp(fopen("/home/ubuntu/AFLNet_MCTS/log.log", "a+"),2);
    return root;
}

seed_info_t* Selection(TreeNode** tree_node)
{
    assert(G_NODE_IS_ROOT(*tree_node));

    *tree_node = select_tree_node(*tree_node);
    prepare_path_str(*tree_node);
//    g_printf("\tTree node selected: ");
//    tree_node_print(tree_node);
//    struct queue_entry * seed_selected = NULL;
    seed_info_t* seed_selected = select_seed(*tree_node);

    return seed_selected;
}

char* Simulation(TreeNode* target)
{
    assert(get_tree_node_data(target)->colour == Golden);
    return NULL;
}

TreeNode* Expansion(TreeNode* tree_node, void* q, u32* response_codes, u32 len_codes, gboolean* is_new)
{
    TreeNode* parent_node;
    *is_new = FALSE;

    // Construct seed with queue_entry q
    seed_info_t* seed = construct_seed_with_queue_entry(q);

    // Check if the response code sequence is new
    // And add the new queue entry to each node along the paths
    char* message = NULL;
    message_format(&message, "response_codes:%s", u32_array_to_str(response_codes, len_codes));
    log_info(message);
    for (u32 i = 0; i < len_codes; i++) {
        parent_node = tree_node;
        if (!(tree_node = exists_child(tree_node, response_codes[i]))){
          *is_new = TRUE;
          tree_node = append_child(parent_node, response_codes[i], White);
        }
        add_seed_to_node(seed, get_simulation_child(tree_node));


        //TODO: cache the path & path_len of each tree_node here
        TreeNodeData* node_data = get_tree_node_data(tree_node);
        node_data->path = response_codes;
        node_data->path_len = i + 1;

        //TODO: assert the path & path_len of each tree_node here
        u32 path_len;
        u32* path = collect_node_path(tree_node, &path_len);
        if (node_data->path_len != path_len){
          printf("\nUnmatch path len:\n");
          printf("\n");
          print_path(tree_node);
          printf("\n");
          printf("\nnode path len: %d; path len: %d\n", node_data->path_len, path_len);
        }
        if (!memcmp(node_data->path, path, node_data->path_len)){
          //TOASK: How are these two arrays different?
          for (int j = 0; j < path_len; ++j) {
            assert(node_data->path[j] == path[j]);
          }
//          printf("\nUnmatch path:\n");
//          printf("Path from responses:\n");
//          for (int j = 0; j < node_data->path_len; ++j) {
//            printf("%d ", node_data->path[j]);
//          }
//          printf("\n");
//          printf("Path from my function:\n");
//          for (int j = 0; j < path_len; ++j) {
//            printf("%d ", path[j]);
//          }
//          printf("\n");
        }
        assert(node_data->path_len == path_len);
//        assert(memcmp(node_data->path, path, path_len));
    }
    parent_node = tree_node;

    /*NOTE: Stats propagation along the execution path is done here*/
    while (parent_node) {
        get_tree_node_data(parent_node)->discovered += *is_new;
        parent_node = parent_node->parent;
    }

    /* NOTE: Stats propagation of the seed is done here */
    seed->discovered += *is_new;
    return tree_node;
}

void Propagation(TreeNode* leaf_selected, seed_info_t* seed_selected, gboolean is_new)
{
    while (leaf_selected) {
      get_tree_node_data(leaf_selected)->discovered += is_new;
      leaf_selected = leaf_selected->parent;
    }
    seed_selected->discovered += is_new;
}

void parent(TreeNode* child, TreeNode** parent){
    /*
     * Takes a pointer to a child node, and a pointer to the pointer of a NULL
     * Then assigned the pointer to the parent of the child to the second parameter
     */
    parent = &child->parent;
}

/* ================================================ MCTS Functions ================================================ */



/* EOF */
