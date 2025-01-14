@echo off

echo Create python venv for reflection parser...
python -m venv ./reflection_parser/parser_env

echo Activate the python env
call ./reflection_parser/parser_env/Scripts/activate.bat

echo install packages required
python -m pip install --upgrade pip
pip install -r ./reflection_parser/requirements.txt
