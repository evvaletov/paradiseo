/*
The Evolving Distribution Objects framework (EDO) is a template-based,
ANSI-C++ evolutionary computation library which helps you to write your
own estimation of distribution algorithms.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

Copyright (C) 2010 Thales group
*/
/*
    Authors:
             Johann Dreo <johann.dreo@thalesgroup.com>
             Caner Candan <caner.candan@thalesgroup.com>
*/

#ifndef _edoNormalMulti_h
#define _edoNormalMulti_h

#include "edoDistrib.h"

#ifdef WITH_BOOST

#include <boost/numeric/ublas/symmetric.hpp>
#include <boost/numeric/ublas/lu.hpp>


namespace ublas = boost::numeric::ublas;

//! edoNormalMulti< EOT >
template < typename EOT >
class edoNormalMulti : public edoDistrib< EOT >
{
public:
    typedef typename EOT::AtomType AtomType;

    edoNormalMulti
    (
     const ublas::vector< AtomType >& mean,
     const ublas::symmetric_matrix< AtomType, ublas::lower >& varcovar
     )
        : _mean(mean), _varcovar(varcovar)
    {
        assert(_mean.size() > 0);
        assert(_mean.size() == _varcovar.size1());
        assert(_mean.size() == _varcovar.size2());
    }

    unsigned int size()
    {
        assert(_mean.size() == _varcovar.size1());
        assert(_mean.size() == _varcovar.size2());
        return _mean.size();
    }

    ublas::vector< AtomType > mean() const {return _mean;}
    ublas::symmetric_matrix< AtomType, ublas::lower > varcovar() const {return _varcovar;}

private:
    ublas::vector< AtomType > _mean;
    ublas::symmetric_matrix< AtomType, ublas::lower > _varcovar;
};


#else
#ifdef WITH_EIGEN

#include <Eigen/Dense>

template < typename EOT >
class edoNormalMulti : public edoDistrib< EOT >
{
public:
    typedef typename EOT::AtomType AtomType;
    typedef Eigen::Matrix< AtomType, 1, Eigen::Dynamic, Eigen::RowMajor> Vector;
    typedef Eigen::Matrix< AtomType, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> Matrix;

    edoNormalMulti(
        const Vector & mean,
        const Matrix & varcovar
    )
        : _mean(mean), _varcovar(varcovar)
    {
        assert(_mean.innerSize() > 0);
        assert(_mean.innerSize() == _varcovar.innerSize());
        assert(_mean.innerSize() == _varcovar.outerSize());
    }

    unsigned int size()
    {
        assert(_mean.innerSize() == _varcovar.innerSize());
        assert(_mean.innerSize() == _varcovar.outerSize());
        return _mean.innerSize();
    }

    Vector mean() const {return _mean;}
    Matrix varcovar() const {return _varcovar;}

private:
    Vector _mean;
    Matrix _varcovar;
};



#endif // WITH_EIGEN
#endif // WITH_BOOST

#endif // !_edoNormalMulti_h
