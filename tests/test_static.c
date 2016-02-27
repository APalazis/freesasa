/*
  Copyright Simon Mitternacht 2013-2016.

  This file is part of FreeSASA.

  FreeSASA is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  FreeSASA is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with FreeSASA.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <check.h>
#include "whole_lib_one_file.c"
const int n_atoms = 6;
const int n_coord = 18;
static const double v[] = {0,0,0, 1,1,1, -1,1,-1, 2,0,-2, 2,2,0, -5,5,5};
static const double r[]  = {4,2,2,2,2,2};


START_TEST (test_cell) {
    double r_max;
    cell_list *c;
    coord_t *coord = freesasa_coord_new();
    freesasa_coord_append(coord,v,n_atoms);
    r_max = max_array(r,n_atoms);
    ck_assert(fabs(r_max-4) < 1e-10);
    c = cell_list_new(r_max,coord);
    ck_assert(c != NULL);
    ck_assert(c->cell != NULL);
    ck_assert(fabs(c->d - r_max) < 1e-10);
    // check bounds
    ck_assert(fabs(c->x_min < -5));
    ck_assert(fabs(c->x_max > 2));
    ck_assert(fabs(c->y_min < 0));
    ck_assert(fabs(c->y_max > 5));
    ck_assert(fabs(c->z_min < -2));
    ck_assert(fabs(c->z_max > 5));
    // check number of cells
    ck_assert(c->nx*c->d >= 7);
    ck_assert(c->nx <= ceil(7/r_max)+1);
    ck_assert(c->ny*c->d >= 5);
    ck_assert(c->ny <= ceil(5/r_max)+1);
    ck_assert(c->nz*c->d >= 7);
    ck_assert(c->nz <= ceil(7/r_max)+1);
    ck_assert_int_eq(c->n, c->nx*c->ny*c->nz);
    // check the individual cells
    int na = 0;
    ck_assert_int_eq(c->cell[0].n_nb,8);
    ck_assert_int_eq(c->cell[c->n-1].n_nb,1);
    for (int i = 0; i < c->n; ++i) {
        cell ci = c->cell[i];
        ck_assert(ci.n_atoms >= 0);
        if (ci.n_atoms > 0) ck_assert(ci.atom != NULL);
        ck_assert_int_ge(ci.n_nb, 1); 
        ck_assert_int_le(ci.n_nb, 17);
        na += ci.n_atoms;
    }
    ck_assert_int_eq(na,n_atoms);
    cell_list_free(c);
    freesasa_coord_free(coord);
}
END_TEST

int is_identical(const double *l1, const double *l2, int n) {
    for (int i = 0; i < n; ++i) {
        if (l1[i] != l2[i]) return 0;
    }
    return 1;
}

int is_sorted(const double *list,int n)
{
    for (int i = 0; i < n - 1; ++i) if (list[2*i] > list[2*i+1]) return 0;
    return 1;
}

START_TEST (test_sort_arcs) {
    double a_ref[] = {0,1,2,3}, b_ref[] = {-2,0,-1,0,-1,1};
    double a1[4] = {0,1,2,3}, a2[4] = {2,3,0,1};
    double b1[6] = {-2,0,-1,0,-1,1}, b2[6] = {-1,1,-2,0,-1,1};
    sort_arcs(a1,2);
    sort_arcs(a2,2);
    sort_arcs(b1,3);
    sort_arcs(b2,3);
    ck_assert(is_sorted(a1,2));
    ck_assert(is_sorted(a2,2));
    ck_assert(is_sorted(b1,3));
    ck_assert(is_sorted(b2,3));
    ck_assert(is_identical(a_ref,a1,4));
    ck_assert(is_identical(a_ref,a2,4));
    ck_assert(is_identical(b_ref,b1,6));
}
END_TEST

START_TEST (test_exposed_arc_length) 
{
    double a1[4] = {0,0.1*TWOPI,0.9*TWOPI,TWOPI}, a2[4] = {0.9*TWOPI,TWOPI,0,0.1*TWOPI};
    double a3[4] = {0,TWOPI,1,2}, a4[4] = {1,2,0,TWOPI};
    double a5[4] = {0.1*TWOPI,0.2*TWOPI,0.5*TWOPI,0.6*TWOPI};
    double a6[4] = {0.1*TWOPI,0.2*TWOPI,0.5*TWOPI,0.6*TWOPI};
    double a7[4] = {0.1*TWOPI,0.3*TWOPI,0.15*TWOPI,0.2*TWOPI};
    double a8[4] = {0.15*TWOPI,0.2*TWOPI,0.1*TWOPI,0.3*TWOPI};
    double a9[10] = {0.05,0.1, 0.5,0.6, 0,0.15, 0.7,0.8, 0.75,TWOPI};
    ck_assert(fabs(exposed_arc_length(a1,2) - 0.8*TWOPI) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a2,2) - 0.8*TWOPI) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a3,2)) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a4,2)) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a5,2) - 0.8*TWOPI) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a6,2) - 0.8*TWOPI) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a7,2) - 0.8*TWOPI) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a8,2) - 0.8*TWOPI) < 1e-10);
    ck_assert(fabs(exposed_arc_length(a9,5) - 0.45) < 1e-10);
    // can't think of anything more qualitatively different here
}
END_TEST

START_TEST (test_classifier) 
{
    const char *strarr[] = {"A","B","C"};
    const char *line[] = {"# Bla"," # Bla","Bla # Bla"," Bla # Bla","#Bla #Alb"};
    char *dummy_str = NULL;
    struct classifier_types *types = classifier_types_new();
    struct classifier_residue *residue_cfg = classifier_residue_new("ALA");
    struct classifier_config *config = classifier_config_new();

    freesasa_set_verbosity(FREESASA_V_SILENT);

    ck_assert_int_eq(find_string((char**)strarr,"A",3),0);
    ck_assert_int_eq(find_string((char**)strarr,"B",3),1);
    ck_assert_int_eq(find_string((char**)strarr,"C",3),2);
    ck_assert_int_eq(find_string((char**)strarr,"D",3),-1);
    ck_assert_int_eq(find_string((char**)strarr," C ",3),2);
    ck_assert_int_eq(find_string((char**)strarr,"CC",3),-1);
    
    ck_assert_int_eq(strip_line(&dummy_str,line[0]),0);
    ck_assert_int_eq(strip_line(&dummy_str,line[1]),0);
    ck_assert_int_eq(strip_line(&dummy_str,line[2]),3);
    ck_assert_str_eq(dummy_str,"Bla");
    ck_assert_int_eq(strip_line(&dummy_str,line[3]),3);
    ck_assert_str_eq(dummy_str,"Bla");
    ck_assert_int_eq(strip_line(&dummy_str,line[4]),0);

    ck_assert_int_eq(types->n_classes, 0);
    ck_assert_int_eq(add_class(types,"A"),0);
    ck_assert_int_eq(types->n_classes, 1);
    ck_assert_str_eq(types->class_name[0], "A");
    ck_assert_int_eq(add_class(types,"A"),0);
    ck_assert_int_eq(types->n_classes, 1);
    ck_assert_int_eq(add_class(types,"B"),1);
    ck_assert_int_eq(types->n_classes, 2);
    ck_assert_str_eq(types->class_name[1], "B");

    classifier_types_free(types);
    types = classifier_types_new();

    ck_assert_int_eq(types->n_types, 0);
    ck_assert_int_eq(add_type(types,"a","A",1.0),0);
    ck_assert_int_eq(add_type(types,"b","B",2.0),1);
    ck_assert_int_eq(add_type(types,"b","B",1.0),FREESASA_WARN);
    ck_assert_int_eq(add_type(types,"c","C",3.0),2);
    ck_assert_int_eq(types->n_types,3);
    ck_assert_int_eq(types->n_classes,3);
    ck_assert_str_eq(types->name[0],"a");
    ck_assert_str_eq(types->name[1],"b");
    ck_assert_str_eq(types->name[2],"c");
    ck_assert_str_eq(types->class_name[0],"A");
    ck_assert_str_eq(types->class_name[1],"B");
    ck_assert_str_eq(types->class_name[2],"C");
    ck_assert(fabs(types->type_radius[0]-1.0) < 1e-10);
    ck_assert(fabs(types->type_radius[1]-2.0) < 1e-10);
    ck_assert(fabs(types->type_radius[2]-3.0) < 1e-10);

    classifier_types_free(types);
    types = classifier_types_new();

    ck_assert_int_eq(read_types_line(types,""),FREESASA_FAIL);
    ck_assert_int_eq(read_types_line(types,"a"),FREESASA_FAIL);
    ck_assert_int_eq(read_types_line(types,"a 1.0"),FREESASA_FAIL);
    ck_assert_int_eq(read_types_line(types,"a b C"),FREESASA_FAIL);
    ck_assert_int_eq(read_types_line(types,"a 1.0 C"),FREESASA_SUCCESS);
    ck_assert_int_eq(read_types_line(types,"b 2.0 D"),FREESASA_SUCCESS);
    ck_assert_int_eq(types->n_types,2);
    ck_assert_int_eq(types->n_classes,2);
    ck_assert_str_eq(types->name[0],"a");
    ck_assert_str_eq(types->class_name[0],"C");
    ck_assert(fabs(types->type_radius[0]-1.0) < 1e-10);

    ck_assert_int_eq(add_atom(residue_cfg,"C",1.0,0),0);
    ck_assert_int_eq(add_atom(residue_cfg,"CB",2.0,0),1);
    ck_assert_int_eq(add_atom(residue_cfg,"CB",2.0,0),FREESASA_WARN);
    ck_assert_str_eq(residue_cfg->atom_name[0],"C");
    ck_assert_str_eq(residue_cfg->atom_name[1],"CB");
    ck_assert(fabs(residue_cfg->atom_radius[0]-1.0) < 1e-10);
    ck_assert(fabs(residue_cfg->atom_radius[1]-2.0) < 1e-10);
    classifier_residue_free(residue_cfg);

    ck_assert_int_eq(add_residue(config,"A"),0);
    ck_assert_int_eq(add_residue(config,"B"),1);
    ck_assert_int_eq(add_residue(config,"B"),1);
    ck_assert_int_eq(config->n_residues,2);
    ck_assert_str_eq(config->residue_name[0],"A");
    ck_assert_str_eq(config->residue_name[1],"B");
    ck_assert_str_eq(config->residue[0]->name,"A");

    classifier_config_free(config);
    config = classifier_config_new();
    
    ck_assert_int_eq(read_atoms_line(config,types,"A A"),FREESASA_FAIL);
    ck_assert_int_eq(read_atoms_line(config,types,"A A A"),FREESASA_FAIL);
    ck_assert_int_eq(read_atoms_line(config,types,"ALA CA a"),FREESASA_SUCCESS);
    ck_assert_int_eq(read_atoms_line(config,types,"ALA CB b"),FREESASA_SUCCESS);
    ck_assert_int_eq(read_atoms_line(config,types,"ARG CA a"),FREESASA_SUCCESS);
    ck_assert_int_eq(read_atoms_line(config,types,"ARG CB b"),FREESASA_SUCCESS);
    ck_assert_int_eq(read_atoms_line(config,types,"ARG CG c"),FREESASA_FAIL);
    config_copy_classes(config, types);
    ck_assert_int_eq(config->n_residues,2);
    ck_assert_int_eq(config->n_classes,2);
    ck_assert_str_eq(config->residue_name[0],"ALA");
    ck_assert_str_eq(config->residue_name[1],"ARG");
    ck_assert_str_eq(config->class_name[0],"C");
    ck_assert_str_eq(config->class_name[1],"D");
    ck_assert_int_eq(config->residue[0]->n_atoms,2);
    ck_assert_str_eq(config->residue[0]->atom_name[0],"CA");
    ck_assert_str_eq(config->residue[0]->atom_name[1],"CB");
    ck_assert(fabs(config->residue[0]->atom_radius[0]-1.0) < 1e-5);
    ck_assert(fabs(config->residue[0]->atom_radius[1]-2.0) < 1e-5);
    
    classifier_config_free(config);
    classifier_types_free(types);

    freesasa_set_verbosity(FREESASA_V_NORMAL);

    free(dummy_str);
}
END_TEST

START_TEST (test_selection) 
{
    static const expression empty_expression = {
        .right = NULL, .left = NULL, .value = NULL, .type = E_SELECTION
    };
    freesasa_structure *structure = freesasa_structure_new();
    freesasa_structure_add_atom(structure," CA ","ALA","   1",'A',0,0,0);
    freesasa_structure_add_atom(structure," O  ","ALA","   1",'A',10,10,10);
    struct selection *s1 = selection_new(freesasa_structure_n(structure));
    struct selection *s2 = selection_new(freesasa_structure_n(structure));
    struct selection *s3 = selection_new(freesasa_structure_n(structure));
    struct selection *s4 = selection_new(freesasa_structure_n(structure));
    expression r,l,e,e_symbol;
    r = l = e = e_symbol = empty_expression;
    e.type = E_PLUS;
    e.right = &r;
    e.left = &l;
    r.value = "C"; r.type = E_ID;
    l.value = "O"; l.type = E_ID;
    e_symbol.type = E_SYMBOL;
    e_symbol.left = &e;

    // select_symbol
    select_list(E_SYMBOL,s1,structure,&r);
    ck_assert_int_eq(s1->atom[0],1);
    ck_assert_int_eq(s1->atom[1],0);
    select_list(E_SYMBOL,s2,structure,&l);
    ck_assert_int_eq(s2->atom[0],0);
    ck_assert_int_eq(s2->atom[1],1);
    select_list(E_SYMBOL,s3,structure,&e);
    ck_assert_int_eq(s3->atom[0],1);
    ck_assert_int_eq(s3->atom[1],1);
    select_atoms(s4,&e_symbol,structure);
    ck_assert_int_eq(s4->atom[0],1);
    ck_assert_int_eq(s4->atom[1],1);

    // selection_join
    selection_join(s3,s1,s2,E_AND);
    ck_assert_int_eq(s3->atom[0],0);
    ck_assert_int_eq(s3->atom[1],0);
    selection_join(s3,s1,s2,E_OR);
    ck_assert_int_eq(s3->atom[0],1);
    ck_assert_int_eq(s3->atom[1],1);
    ck_assert_int_eq(selection_join(NULL,s1,s2,E_OR),FREESASA_FAIL);
    ck_assert_ptr_eq(selection_join(s3,NULL,s1,E_OR),FREESASA_FAIL);
    ck_assert_ptr_eq(selection_join(NULL,NULL,NULL,E_OR),FREESASA_FAIL);
    
    //selection_not
    ck_assert_int_eq(selection_not(s3),FREESASA_SUCCESS);
    ck_assert_int_eq(s3->atom[0],0);
    ck_assert_int_eq(s3->atom[1],0);
    ck_assert_int_eq(selection_not(NULL),FREESASA_FAIL);
    
}
END_TEST

START_TEST (test_expression)
{
    expression *e = get_expression("c1, symbol O+C");
    ck_assert_ptr_ne(e,NULL);
    ck_assert_int_eq(e->type, E_SELECTION);
    ck_assert_ptr_ne(e->left, NULL);
    ck_assert_ptr_eq(e->right, NULL);
    ck_assert_str_eq(e->value, "c1");
    ck_assert_int_eq(e->left->type,E_SYMBOL);
    ck_assert_ptr_ne(e->left->left,NULL);
    ck_assert_ptr_eq(e->left->right,NULL);
    ck_assert_int_eq(e->left->left->type,E_PLUS);
    ck_assert_int_eq(e->left->left->left->type,E_ID);
    ck_assert_int_eq(e->left->left->right->type,E_ID);
    ck_assert_str_eq(e->left->left->right->value,"C");
    ck_assert_str_eq(e->left->left->left->value,"O");
}
END_TEST

int main(int argc, char **argv) 
{
    Suite *s = suite_create("Tests of static functions");

    TCase *nb = tcase_create("nb.c");
    tcase_add_test(nb,test_cell);
    suite_add_tcase(s, nb);

    TCase *lr = tcase_create("sasa_lr.c");
    tcase_add_test(lr,test_sort_arcs);
    tcase_add_test(lr,test_exposed_arc_length);
    suite_add_tcase(s, lr);

    TCase *classifier = tcase_create("classifier.c");
    tcase_add_test(classifier,test_classifier);
    suite_add_tcase(s, classifier);

    TCase *selector = tcase_create("selector.c");
    tcase_add_test(selector,test_selection);
    tcase_add_test(selector,test_expression);
    suite_add_tcase(s, selector);

    SRunner *sr = srunner_create(s);
    srunner_run_all(sr,CK_VERBOSE);

    return (srunner_ntests_failed(sr) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
