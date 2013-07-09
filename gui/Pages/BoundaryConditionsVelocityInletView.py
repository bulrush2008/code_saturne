# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------------

# This file is part of Code_Saturne, a general-purpose CFD tool.
#
# Copyright (C) 1998-2013 EDF S.A.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301, USA.

#-------------------------------------------------------------------------------

"""
This module contains the following classes:
- BoundaryConditionsVelocityInletView
"""

#-------------------------------------------------------------------------------
# Standard modules
#-------------------------------------------------------------------------------

import string, logging

#-------------------------------------------------------------------------------
# Third-party modules
#-------------------------------------------------------------------------------
import sys
if sys.version_info[0] == 2:
    import sip
    sip.setapi('QString', 2)

from PyQt4.QtCore import *
from PyQt4.QtGui  import *

#-------------------------------------------------------------------------------
# Application modules import
#-------------------------------------------------------------------------------

from Pages.BoundaryConditionsVelocityInletForm import Ui_BoundaryConditionsVelocityInletForm

from Base.Toolbox import GuiParam
from Base.QtPage import DoubleValidator, ComboModel, setGreenColor
from Pages.LocalizationModel import LocalizationModel, Zone
from Pages.Boundary import Boundary
from Pages.CompressibleModel import CompressibleModel
from Pages.GasCombustionModel import GasCombustionModel

from Pages.QMeiEditorView import QMeiEditorView

#-------------------------------------------------------------------------------
# log config
#-------------------------------------------------------------------------------

logging.basicConfig()
log = logging.getLogger("BoundaryConditionsVelocityInletView")
log.setLevel(GuiParam.DEBUG)

#-------------------------------------------------------------------------------
# Main class
#-------------------------------------------------------------------------------

class BoundaryConditionsVelocityInletView(QWidget, Ui_BoundaryConditionsVelocityInletForm):
    """
    Boundary condition for velocity in inlet, without particular physics.
    """
    def __init__(self, parent):
        """
        Constructor
        """
        QWidget.__init__(self, parent)

        Ui_BoundaryConditionsVelocityInletForm.__init__(self)
        self.setupUi(self)
        self.thermodynamic_list = ['Pressure', 'Density', 'Temperature', 'Energy']


    def setup(self, case):
        """
        Setup the widget
        """
        self.__case = case
        self.__boundary = None

        self.__case.undoStopGlobal()

        self.mdl = CompressibleModel(self.__case)
        self.gas = GasCombustionModel(self.__case)

        # Connections
        self.connect(self.comboBoxVelocity, SIGNAL("activated(const QString&)"), self.__slotChoiceVelocity)
        self.connect(self.lineEditVelocity, SIGNAL("textChanged(const QString &)"), self.__slotVelocityValue)

        self.connect(self.comboBoxDirection,  SIGNAL("activated(const QString&)"), self.__slotChoiceDirection)
        self.connect(self.lineEditDirectionX, SIGNAL("textChanged(const QString &)"), self.__slotDirX)
        self.connect(self.lineEditDirectionY, SIGNAL("textChanged(const QString &)"), self.__slotDirY)
        self.connect(self.lineEditDirectionZ, SIGNAL("textChanged(const QString &)"), self.__slotDirZ)

        self.connect(self.comboBoxTypeInlet,    SIGNAL("activated(const QString&)"),    self.__slotInletType)
        self.connect(self.checkBoxPressure,     SIGNAL("clicked()"),                    self.__slotPressure)
        self.connect(self.checkBoxDensity,      SIGNAL("clicked()"),                    self.__slotDensity)
        self.connect(self.checkBoxTemperature,  SIGNAL("clicked()"),                    self.__slotTemperature)
        self.connect(self.checkBoxEnergy,       SIGNAL("clicked()"),                    self.__slotEnergy)
        self.connect(self.lineEditPressure,     SIGNAL("textChanged(const QString &)"), self.__slotPressureValue)
        self.connect(self.lineEditDensity,      SIGNAL("textChanged(const QString &)"), self.__slotDensityValue)
        self.connect(self.lineEditDensity2,     SIGNAL("textChanged(const QString &)"), self.__slotDensity2Value)
        self.connect(self.lineEditTemperature,  SIGNAL("textChanged(const QString &)"), self.__slotTemperatureValue)
        self.connect(self.lineEditEnergy,       SIGNAL("textChanged(const QString &)"), self.__slotEnergyValue)

        self.connect(self.comboBoxTypeInletGasComb,   SIGNAL("activated(const QString&)"), self.__slotInletTypeGasComb)
        self.connect(self.lineEditTemperatureGasComb, SIGNAL("textChanged(const QString &)"),  self.__slotTemperatureGasComb)
        self.connect(self.lineEditFraction,           SIGNAL("textChanged(const QString &)"),  self.__slotMeanMixtureFraction)

        # Combo models
        self.modelVelocity = ComboModel(self.comboBoxVelocity, 6, 1)
        self.modelVelocity.addItem(self.tr("norm"), 'norm')
        self.modelVelocity.addItem(self.tr("mass flow rate"), 'flow1')
        self.modelVelocity.addItem(self.tr("volumic flow rate"), 'flow2')
        self.modelVelocity.addItem(self.tr("norm (user law)"), 'norm_formula')
        self.modelVelocity.addItem(self.tr("mass flow rate (user law)"), 'flow1_formula')
        self.modelVelocity.addItem(self.tr("volumic flow rate (user law)"), 'flow2_formula')

        self.modelDirection = ComboModel(self.comboBoxDirection, 3, 1)
        self.modelDirection.addItem(self.tr("normal direction to the inlet"), 'normal')
        self.modelDirection.addItem(self.tr("specified coordinates"), 'coordinates')
        self.modelDirection.addItem(self.tr("user profile"), 'formula')

        self.modelTypeInlet = ComboModel(self.comboBoxTypeInlet, 2, 1)
        self.modelTypeInlet.addItem(self.tr("imposed inlet"), 'imposed_inlet')
        self.modelTypeInlet.addItem(self.tr("subsonic inlet"), 'subsonic_inlet')

        self.modelTypeInletGasComb = ComboModel(self.comboBoxTypeInletGasComb, 2, 1)
        model = self.gas.getGasCombustionModel()
        if model == 'lwp' or model =='ebu':
            self.modelTypeInletGasComb.addItem(self.tr("Unburned gas"), 'unburned')
            self.modelTypeInletGasComb.addItem(self.tr("Burned gas"), 'burned')
        elif model == 'd3p':
            self.modelTypeInletGasComb.addItem(self.tr("Oxydant"), 'oxydant')
            self.modelTypeInletGasComb.addItem(self.tr("Fuel"), 'fuel')

        # Validators
        validatorVelocity = DoubleValidator(self.lineEditVelocity)
        validatorX = DoubleValidator(self.lineEditDirectionX)
        validatorY = DoubleValidator(self.lineEditDirectionY)
        validatorZ = DoubleValidator(self.lineEditDirectionZ)
        validatorP = DoubleValidator(self.lineEditPressure, min = 0.0)
        validatorD = DoubleValidator(self.lineEditDensity, min = 0.0)
        validatorT = DoubleValidator(self.lineEditTemperature, min = 0.0)
        validatorE = DoubleValidator(self.lineEditEnergy, min = 0.0)
        validatorD2 = DoubleValidator(self.lineEditDensity2, min = 0.0)
        validatorTemp = DoubleValidator(self.lineEditTemperatureGasComb, min=0.)
        validatorFrac = DoubleValidator(self.lineEditFraction, min=0., max=1.)

        # Apply validators
        self.lineEditVelocity.setValidator(validatorVelocity)
        self.lineEditDirectionX.setValidator(validatorX)
        self.lineEditDirectionY.setValidator(validatorY)
        self.lineEditDirectionZ.setValidator(validatorZ)
        self.lineEditPressure.setValidator(validatorP)
        self.lineEditDensity.setValidator(validatorD)
        self.lineEditTemperature.setValidator(validatorT)
        self.lineEditEnergy.setValidator(validatorE)
        self.lineEditDensity2.setValidator(validatorD2)
        self.lineEditTemperatureGasComb.setValidator(validatorTemp)
        self.lineEditFraction.setValidator(validatorFrac)

        self.connect(self.pushButtonVelocityFormula, SIGNAL("clicked()"), self.__slotVelocityFormula)
        self.connect(self.pushButtonDirectionFormula, SIGNAL("clicked()"), self.__slotDirectionFormula)

        self.__case.undoStartGlobal()


    def showWidget(self, boundary):
        """
        Show the widget
        """
        self.__boundary = boundary

        # Initialize velocity
        choice = self.__boundary.getVelocityChoice()
        self.modelVelocity.setItem(str_model=choice)
        self.__updateLabel()

        if choice[-7:] == "formula":
            self.pushButtonVelocityFormula.setEnabled(True)
            self.lineEditVelocity.setEnabled(False)
        else:
            self.pushButtonVelocityFormula.setEnabled(False)
            self.lineEditVelocity.setEnabled(True)
            v = self.__boundary.getVelocity()
            self.lineEditVelocity.setText(str(v))

        # Initialize direction
        choice = self.__boundary.getDirectionChoice()
        self.modelDirection.setItem(str_model=choice)
        text = self.modelDirection.dicoM2V[choice]
        if choice == "formula":
            self.pushButtonDirectionFormula.setEnabled(True)
            self.frameDirectionCoordinates.hide()
        elif choice == "coordinates":
            self.pushButtonDirectionFormula.setEnabled(False)
            self.frameDirectionCoordinates.show()
            v = self.__boundary.getDirection('direction_x')
            self.lineEditDirectionX.setText(str(v))
            v = self.__boundary.getDirection('direction_y')
            self.lineEditDirectionY.setText(str(v))
            v = self.__boundary.getDirection('direction_z')
            self.lineEditDirectionZ.setText(str(v))
        elif choice == "normal":
            self.pushButtonDirectionFormula.setEnabled(False)
            self.frameDirectionCoordinates.hide()

        self.initialize()


    def initialize(self):
        """
        Initialize widget for compressible
        """
        # Initialize thermodynamic value
        if self.mdl.getCompressibleModel() != 'off':
            inlet_type = self.__boundary.getInletType()
            self.modelTypeInlet.setItem(str_model = inlet_type)
            self.__boundary.setInletType(inlet_type)

            if inlet_type == 'imposed_inlet':
                self.groupBoxThermodynamic.show()
                self.frameDensity.hide()
                box_list = self.__boundary.getCheckedBoxList()
                if box_list == []:
                    for name in self.thermodynamic_list:
                        __checkBox = getattr(self, "checkBox" + name)
                        __lineEdit = getattr(self, "lineEdit" + name)
                        __checkBox.setChecked(False)
                        __lineEdit.setEnabled(False)
                        __lineEdit.setText(str(""))
                elif len(box_list) == 1:
                    box = box_list[0]
                    for name in self.thermodynamic_list:
                        if name != box:
                            __checkBox = getattr(self, "checkBox" + name)
                            __lineEdit = getattr(self, "lineEdit" + name)
                            __checkBox.setChecked(False)
                            __lineEdit.setEnabled(False)
                            __lineEdit.setText(str(""))
                    if box == 'Temperature':
                        self.checkBoxEnergy.setEnabled(False)
                        self.lineEditEnergy.setEnabled(False)
                        self.lineEditEnergy.setText(str(""))
                    elif box == 'Energy':
                        self.checkBoxTemperature.setEnabled(False)
                        self.lineEditTemperature.setEnabled(False)
                        self.lineEditTemperature.setText(str(""))
                    __checkBox = getattr(self, "checkBox" + box)
                    __checkBox.setChecked(True)
                    __lineEdit = getattr(self, "lineEdit" + box)
                    __lineEdit.setEnabled(True)
                    v1 = self.__boundary.getListValue()[0]
                    __lineEdit.setText(str(v1))
                elif len(box_list) == 2:
                    box1 = box_list[0]
                    box2 = box_list[1]
                    for name in self.thermodynamic_list:
                        if name not in box_list:
                            __checkBox = getattr(self, "checkBox" + name)
                            __lineEdit = getattr(self, "lineEdit" + name)
                            __checkBox.setChecked(False)
                            __checkBox.setEnabled(False)
                            __lineEdit.setEnabled(False)
                            __lineEdit.setText(str(""))
                    v1,v2 = self.__boundary.getListValue()
                    for name in box_list:
                        __checkBox = getattr(self, "checkBox" + name)
                        __lineEdit = getattr(self, "lineEdit" + name)
                        __checkBox.setChecked(True)
                        __lineEdit.setEnabled(True)
                        if v1 >= 0.:
                            __lineEdit.setText(str(v1))
                        else:
                            __lineEdit.setText(str(v2))
                            __lineEdit.setText(str(v2))
                        v1 = -1.
            else:
                self.groupBoxThermodynamic.hide()
                self.frameDensity.show()
                density = self.__boundary.getThermoValue('density')
                self.lineEditDensity2.setText(str(density))
        else:
            self.groupBoxCompressible.hide()


        # Initialize temperature and mean mixture fraction
        model = self.gas.getGasCombustionModel()
        if model != 'off':
            self.groupBoxGasCombustion.show()
            inlet_type = self.__boundary.getInletGasCombustionType()
            self.modelTypeInletGasComb.setItem(str_model = inlet_type)

            if model == 'd3p':
                self.lineEditTemperatureGasComb.hide()
                self.labelTemperature_2.hide()
                self.labelUnitTemp.hide()
                self.lineEditFraction.setEnabled(False)
                f = self.__boundary.setMeanMixtureFraction(1)
                self.lineEditFraction.setText(str(1) if inlet_type == 'oxydant' else str(0))
            else :
                self.lineEditTemperatureGasComb.show()
                self.labelTemperature_2.show()
                self.labelUnitTemp.show()
                t = self.__boundary.getGasCombustionTemperature()
                self.lineEditTemperatureGasComb.setText(str(t))
                self.lineEditFraction.setEnabled(True)
                f = self.__boundary.getMeanMixtureFraction()
                self.lineEditFraction.setText(str(f))
        else:
            self.groupBoxGasCombustion.hide()

        self.show()


    def hideWidget(self):
        """
        Hide all
        """
        self.hide()


    @pyqtSignature("const QString&")
    def __slotChoiceVelocity(self, text):
        """
        Private slot.

        Input the velocity boundary type choice (norm, ).

        @type text: C{QString}
        @param text: velocity boundary type choice.
        """
        c = self.modelVelocity.dicoV2M[str(text)]
        log.debug("slotChoiceVelocity: %s " % c)
        self.__boundary.setVelocityChoice(c)

        if c[-7:] == "formula":
            self.pushButtonVelocityFormula.setEnabled(True)
            setGreenColor(self.pushButtonVelocityFormula, True)
            self.lineEditVelocity.setEnabled(False)
            self.lineEditVelocity.setText("")
        else:
            self.pushButtonVelocityFormula.setEnabled(False)
            setGreenColor(self.pushButtonVelocityFormula, False)
            self.lineEditVelocity.setEnabled(True)
            v = self.__boundary.getVelocity()
            self.lineEditVelocity.setText(str(v))

        self.__updateLabel()


    def __updateLabel(self):
        """
        Update the unit for the velocity specification.
        """
        c = self.__boundary.getVelocityChoice()
        if c in ('norm', 'norm_formula'):
            self.labelUnitVelocity.setText(str('m/s'))
        elif c in ('flow1', 'flow1_formula'):
            self.labelUnitVelocity.setText(str('kg/s'))
        elif c in ('flow2', 'flow2_formula'):
            self.labelUnitVelocity.setText(str('m<sup>3</sup>/s'))


    @pyqtSignature("const QString&")
    def __slotVelocityValue(self, text):
        """
        Private slot.

        New value associated to the velocity boundary type.

        @type text: C{QString}
        @param text: value
        """
        v = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setVelocity(v)


    @pyqtSignature("")
    def __slotVelocityFormula(self):
        """
        """
        exp = self.__boundary.getVelocity()
        c = self.__boundary.getVelocityChoice()
        req = [('u_norm', 'Norm of the velocity')]
        if c == 'norm_formula':
            exa = "u_norm = 1.0;"
        elif c == 'flow1_formula':
            exa = "q_m = 1.0;"
        elif c == 'flow2_formula':
            exa = "q_v = 1.0;"

        sym = [('x', "X face's gravity center"),
               ('y', "Y face's gravity center"),
               ('z', "Z face's gravity center"),
               ('dt', 'time step'),
               ('t', 'current time'),
               ('iter', 'number of iteration')]

        dialog = QMeiEditorView(self,
                                check_syntax = self.__case['package'].get_check_syntax(),
                                expression = exp,
                                required   = req,
                                symbols    = sym,
                                examples   = exa)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("slotFormulaVelocity -> %s" % str(result))
            self.__boundary.setVelocity(result)
            setGreenColor(self.pushButtonVelocityFormula, False)


    @pyqtSignature("const QString&")
    def __slotChoiceDirection(self, text):
        """
        Input the direction type choice.
        """
        c = self.modelDirection.dicoV2M[str(text)]
        log.debug("slotChoiceVelocity: %s " % c)
        self.__boundary.setDirectionChoice(c)

        if c == "formula":
            self.pushButtonDirectionFormula.setEnabled(True)
            setGreenColor(self.pushButtonDirectionFormula, True)
            self.frameDirectionCoordinates.hide()
        elif c == "coordinates":
            self.pushButtonDirectionFormula.setEnabled(False)
            setGreenColor(self.pushButtonDirectionFormula, False)
            self.frameDirectionCoordinates.show()
            v = self.__boundary.getDirection('direction_x')
            self.lineEditDirectionX.setText(str(v))
            v = self.__boundary.getDirection('direction_y')
            self.lineEditDirectionY.setText(str(v))
            v = self.__boundary.getDirection('direction_z')
            self.lineEditDirectionZ.setText(str(v))
        elif c == "normal":
            self.pushButtonDirectionFormula.setEnabled(False)
            setGreenColor(self.pushButtonDirectionFormula, False)
            self.frameDirectionCoordinates.hide()


    @pyqtSignature("const QString&")
    def __slotDirX(self, text):
        """
        INPUT value into direction of inlet flow
        """
        value = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setDirection('direction_x', value)


    @pyqtSignature("const QString&")
    def __slotDirY(self, text):
        """
        INPUT value into direction of inlet flow
        """
        value = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setDirection('direction_y', value)


    @pyqtSignature("const QString&")
    def __slotDirZ(self, text):
        """
        INPUT value into direction of inlet flow
        """
        value = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setDirection('direction_z', value)


    @pyqtSignature("")
    def __slotDirectionFormula(self):
        """
        """
        exp = self.__boundary.getDirection('direction_formula')

        req = [('dir_x', 'Direction of the flow along X'),
               ('dir_y', 'Direction of the flow along Y'),
               ('dir_z', 'Direction of the flow along Z')]

        exa = "dir_x = 3.0;\ndir_y = 1.0;\ndir_z = 0.0;\n"

        sym = [('x', "X face's gravity center"),
               ('y', "Y face's gravity center"),
               ('z', "Z face's gravity center"),
               ('dt', 'time step'),
               ('t', 'current time'),
               ('iter', 'number of iteration')]

        dialog = QMeiEditorView(self,
                                check_syntax = self.__case['package'].get_check_syntax(),
                                expression = exp,
                                required   = req,
                                symbols    = sym,
                                examples   = exa)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("slotFormulaDirection -> %s" % str(result))
            self.__boundary.setDirection('direction_formula', result)
            setGreenColor(self.pushButtonDirectionFormula, False)


    @pyqtSignature("const QString&")
    def __slotInletType(self, text):
        """
        INPUT inlet type : 'oxydant'/'fuel' or 'burned'/'unburned'
        """
        value = self.modelTypeInlet.dicoV2M[str(text)]
        log.debug("__slotInletType value = %s " % value)

        self.__boundary.setInletType(value)
        self.initialize()


    @pyqtSignature("")
    def __slotPressure(self):
        """
        Pressure selected or not for the initialisation.
        """
        if self.checkBoxPressure.isChecked():
            self.__boundary.setThermoStatus('pressure', "on")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditPressure.setEnabled(True)
            if len(box_list) == 2:
                for name in self.thermodynamic_list:
                    if name not in box_list:
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(False)
            self.lineEditPressure.setText(str(self.__boundary.getThermoValue('pressure')))
        else:
            self.__boundary.setThermoStatus('pressure', "off")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditPressure.setEnabled(False)
            if len(box_list) == 1:
                for name in self.thermodynamic_list:
                    if name != 'Pressure':
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(True)
                if box_list[0] =='Energy':
                    self.checkBoxTemperature.setEnabled(False)
                if box_list[0] =='Temperature':
                    self.checkBoxEnergy.setEnabled(False)


    @pyqtSignature("")
    def __slotDensity(self):
        """
        Density selected or not for the initialisation.
        """
        if self.checkBoxDensity.isChecked():
            self.__boundary.setThermoStatus('density', "on")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditDensity.setEnabled(True)
            if len(box_list) == 2:
                for name in self.thermodynamic_list:
                    if name not in box_list:
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(False)
            self.lineEditDensity.setText(str(self.__boundary.getThermoValue('density')))
        else:
            self.__boundary.setThermoStatus('density', "off")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditDensity.setEnabled(False)
            if len(box_list) == 1:
                for name in self.thermodynamic_list:
                    if name != 'Density':
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(True)
                if box_list[0] =='Energy':
                    self.checkBoxTemperature.setEnabled(False)
                if box_list[0] =='Temperature':
                    self.checkBoxEnergy.setEnabled(False)


    @pyqtSignature("")
    def __slotTemperature(self):
        """
        Temperature selected or not for the initialisation.
        """
        if self.checkBoxTemperature.isChecked():
            self.__boundary.setThermoStatus('temperature', "on")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditTemperature.setEnabled(True)
            if len(box_list) == 2:
                for name in self.thermodynamic_list:
                    if name not in box_list:
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(False)
            self.checkBoxEnergy.setEnabled(False)
            self.lineEditTemperature.setText(str(self.__boundary.getThermoValue('temperature')))
        else:
            self.__boundary.setThermoStatus('temperature', "off")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditTemperature.setEnabled(False)
            if len(box_list) == 1:
                for name in self.thermodynamic_list:
                    if name != 'Temperature':
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(True)
            self.checkBoxEnergy.setEnabled(True)


    @pyqtSignature("")
    def __slotEnergy(self):
        """
        Energy selected or not for the initialisation.
        """
        if self.checkBoxEnergy.isChecked():
            self.__boundary.setThermoStatus('energy', "on")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditEnergy.setEnabled(True)
            if len(box_list) == 2:
                for name in self.thermodynamic_list:
                    if name not in box_list:
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(False)
            if len(box_list) == 1:
                self.checkBoxTemperature.setEnabled(False)
            self.lineEditEnergy.setText(str(self.__boundary.getThermoValue('energy')))
        else:
            self.__boundary.setThermoStatus('energy', "off")
            box_list = self.__boundary.getCheckedBoxList()
            self.lineEditEnergy.setEnabled(False)
            if len(box_list) == 1:
                for name in self.thermodynamic_list:
                    if name != 'Energy':
                        __checkBox = getattr(self, "checkBox" + name)
                        __checkBox.setEnabled(True)
            self.checkBoxTemperature.setEnabled(True)


    @pyqtSignature("const QString&")
    def __slotPressureValue(self, text):
        """
        INPUT inlet Pressure
        """
        t = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setThermoValue('pressure', t)


    @pyqtSignature("const QString&")
    def __slotDensityValue(self, text):
        """
        INPUT inlet Density
        """
        t = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setThermoValue('density', t)


    @pyqtSignature("const QString&")
    def __slotTemperatureValue(self, text):
        """
        INPUT inlet Temperature
        """
        t = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setThermoValue('temperature', t)


    @pyqtSignature("const QString&")
    def __slotEnergyValue(self, text):
        """
        INPUT inlet Energy
        """
        t = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setThermoValue('energy', t)


    @pyqtSignature("const QString&")
    def __slotDensity2Value(self, text):
        """
        INPUT inlet Density
        """
        t = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setThermoValue('density', t)


    @pyqtSignature("const QString&")
    def __slotTemperatureGasComb(self, text):
        """
        INPUT inlet temperature
        """
        t = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setGasCombustionTemperature(t)


    @pyqtSignature("const QString&")
    def __slotMeanMixtureFraction(self, text):
        """
        INPUT inlet mean mixutre fraction
        """
        f = float(text)
        if self.sender().validator().state == QValidator.Acceptable:
            self.__boundary.setMeanMixtureFraction(f)


    @pyqtSignature("const QString&")
    def __slotInletTypeGasComb(self, text):
        """
        INPUT inlet type : 'oxydant'/'fuel' or 'burned'/'unburned'
        """
        value = self.modelTypeInletGasComb.dicoV2M[str(text)]
        log.debug("__slotInletTypeGasComb value = %s " % value)
        self.__boundary.setInletGasCombustionType(value)
        self.initialize()


    def tr(self, text):
        """
        Translation
        """
        return text

#-------------------------------------------------------------------------------
# End
#-------------------------------------------------------------------------------
